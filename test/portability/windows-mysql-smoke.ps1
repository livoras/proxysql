param(
    [Parameter(Mandatory = $true)][string]$ProxySqlExe,
    [Parameter(Mandatory = $true)][string]$MySqlBin,
    [string]$Scratch = (Join-Path $env:TEMP "proxysql-smoke-$([guid]::NewGuid())"),
    [int]$AdminPort = 16032,
    [int]$ProxyPort = 16033,
    [int]$BackendPort = 13306
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$ProxySqlExe = (Resolve-Path $ProxySqlExe).Path
$server = Join-Path $MySqlBin 'mysqld.exe'
$client = Join-Path $MySqlBin 'mysql.exe'
$mysqlAdmin = Join-Path $MySqlBin 'mysqladmin.exe'
$savedPassword = $env:MYSQL_PWD
$password = [guid]::NewGuid().ToString('N')
$backend = $null
$proxy = $null
$adminReady = $false
$data = Join-Path $Scratch 'mysql'
$proxyData = Join-Path $Scratch 'proxy'

function Wait-Listener($Process, $Port) {
    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        if ($Process.HasExited) {
            throw "Process $($Process.Id) exited with code $($Process.ExitCode) before listening on $Port"
        }
        if (Get-NetTCPConnection -OwningProcess $Process.Id -LocalPort $Port -State Listen -ErrorAction SilentlyContinue) {
            return
        }
        Start-Sleep -Milliseconds 250
    }
    throw "Process $($Process.Id) did not listen on $Port"
}

function Start-Backend($StartNumber) {
    $serverArgs = @(
        '--no-defaults', '--no-monitor', ('--datadir="{0}"' -f $data), '--console',
        '--bind-address=127.0.0.1', "--port=$BackendPort", '--mysqlx=OFF', '--skip-log-bin',
        '--innodb-buffer-pool-size=64M', '--performance-schema=OFF', '--max-connections=32'
    )
    Start-Process $server -ArgumentList $serverArgs -PassThru -RedirectStandardOutput "$Scratch/mysql-$StartNumber.out" -RedirectStandardError "$Scratch/mysql-$StartNumber.err"
}

function Stop-Backend {
    $env:MYSQL_PWD = ''
    & $mysqlAdmin --no-defaults --protocol=TCP '--host=127.0.0.1' "-P$BackendPort" -uroot --connect-timeout=2 shutdown
    if ($LASTEXITCODE -ne 0 -or !$backend.WaitForExit(10000)) { throw 'Scratch MySQL did not shut down' }
}

function Invoke-MySql($Port, $User, $Sql) {
    $result = & $client --no-defaults --protocol=TCP '--host=127.0.0.1' "-P$Port" "-u$User" --connect-timeout=3 --ssl-mode=DISABLED --batch --skip-column-names "--execute=$Sql"
    if ($LASTEXITCODE -ne 0) { throw "mysql.exe failed on port $Port with exit code $LASTEXITCODE" }
    return $result
}

function Assert-Handshake {
    $tcp = [Net.Sockets.TcpClient]::new('127.0.0.1', $ProxyPort)
    try {
        $stream = $tcp.GetStream()
        $stream.ReadTimeout = 3000
        $reader = [IO.BinaryReader]::new($stream)
        $header = $reader.ReadBytes(5)
        if ($header.Length -ne 5 -or $header[3] -ne 0 -or $header[4] -ne 10) {
            throw 'Invalid initial MySQL protocol packet'
        }
    } finally {
        $tcp.Dispose()
    }
}

$ports = @($AdminPort, $ProxyPort, $BackendPort)
if (@($ports | Select-Object -Unique).Count -ne 3) { throw 'Use three distinct test ports' }
if (Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue |
    Where-Object { $_.LocalPort -in $ports }) { throw 'One of the requested test ports is in use' }
if (Test-Path $Scratch) { throw "Scratch directory already exists: $Scratch" }
New-Item -ItemType Directory -Path $Scratch, $proxyData | Out-Null

try {
    $initArgs = @('--no-defaults', '--initialize-insecure', ('--datadir="{0}"' -f $data), '--console', '--innodb-buffer-pool-size=64M')
    $init = Start-Process $server -ArgumentList $initArgs -PassThru -Wait -RedirectStandardOutput "$Scratch/mysql-init.out" -RedirectStandardError "$Scratch/mysql-init.err"
    if ($init.ExitCode -ne 0) { throw "Scratch MySQL initialization failed: $($init.ExitCode)" }

    $backend = Start-Backend 1
    Wait-Listener $backend $BackendPort
    $env:MYSQL_PWD = ''
    Invoke-MySql $BackendPort root @"
CREATE DATABASE proxy_smoke;
CREATE TABLE proxy_smoke.probe (value varchar(20) NOT NULL);
INSERT INTO proxy_smoke.probe VALUES ('windows-backend');
CREATE USER 'probe'@'127.0.0.1' IDENTIFIED BY '$password';
GRANT SELECT ON proxy_smoke.* TO 'probe'@'127.0.0.1';
"@

    $config = Join-Path $Scratch 'proxysql.cfg'
    $configData = $proxyData.Replace('\', '/')
    @"
datadir="$configData"
admin_variables =
{
    admin_credentials="smoke_admin:$password"
    mysql_ifaces="127.0.0.1:$AdminPort"
}
mysql_variables =
{
    interfaces="127.0.0.1:$ProxyPort"
    threads=2
    monitor_enabled=false
    connect_timeout_server=500
    connect_timeout_server_max=2500
    connect_retries_on_failure=1
}
mysql_servers = ( { address="127.0.0.1"; port=$BackendPort; hostgroup=10; use_ssl=1; } )
mysql_users = ( { username="probe"; password="$password"; default_hostgroup=10; } )
"@ | Set-Content $config -Encoding Ascii

    $proxyArgs = @('-f', '-M', '--no-version-check', '-c', ('"{0}"' -f $config))
    $proxy = Start-Process $ProxySqlExe -ArgumentList $proxyArgs -WorkingDirectory (Split-Path $ProxySqlExe) -PassThru -RedirectStandardOutput "$Scratch/proxysql.out" -RedirectStandardError "$Scratch/proxysql.err"
    Wait-Listener $proxy $AdminPort
    Wait-Listener $proxy $ProxyPort
    $env:MYSQL_PWD = $password
    $configuredPort = Invoke-MySql $AdminPort smoke_admin 'SELECT port FROM runtime_mysql_servers WHERE hostgroup_id=10'
    if ($configuredPort -ne "$BackendPort") { throw "Unexpected backend in admin interface: $configuredPort" }
    $adminReady = $true
    Assert-Handshake
    Write-Output 'MySQL protocol handshake and admin query passed'

    $query = 'SELECT value, @@port FROM proxy_smoke.probe'
    $expected = 'windows-backend' + [char]9 + $BackendPort
    $result = Invoke-MySql $ProxyPort probe $query
    if ($result -ne $expected) { throw "Unexpected forwarded result: $result" }
    Write-Output "Forwarded query returned $result"

    Stop-Backend
    $env:MYSQL_PWD = $password
    $failedArgs = @('--no-defaults', '--protocol=TCP', '--host=127.0.0.1', "-P$ProxyPort", '-uprobe',
        '--connect-timeout=3', '--ssl-mode=DISABLED', ('--execute="{0}"' -f $query))
    $failedQuery = Start-Process $client -ArgumentList $failedArgs -PassThru -RedirectStandardOutput "$Scratch/interruption.out" -RedirectStandardError "$Scratch/interruption.err"
    if (!$failedQuery.WaitForExit(10000)) {
        $failedQuery.Kill()
        throw 'Query did not report the backend outage within 10 seconds'
    }
    if ($failedQuery.ExitCode -eq 0 -or $proxy.HasExited) { throw 'Backend outage was not isolated to the query' }
    Write-Output 'Query failed during backend outage while ProxySQL stayed up'

    $backend = Start-Backend 2
    Wait-Listener $backend $BackendPort
    $result = Invoke-MySql $ProxyPort probe $query
    if ($result -ne $expected -or $proxy.HasExited) { throw "Unexpected recovery result: $result" }
    Write-Output "Query after backend restart returned $result"
} finally {
    if ($proxy -and !$proxy.HasExited) {
        if ($adminReady) {
            $env:MYSQL_PWD = $password
            $shutdownArgs = @('--no-defaults', '--protocol=TCP', '--host=127.0.0.1', "-P$AdminPort",
                '-usmoke_admin', '--connect-timeout=3', '--ssl-mode=DISABLED', '--execute="PROXYSQL SHUTDOWN SLOW"')
            # ProxySQL closes the admin connection before acknowledging a shutdown command.
            Start-Process $client -ArgumentList $shutdownArgs -Wait -RedirectStandardOutput "$Scratch/shutdown.out" -RedirectStandardError "$Scratch/shutdown.err"
        }
        if (!$proxy.WaitForExit(5000)) {
            $proxy.Kill()
            Write-Warning 'ProxySQL required forced cleanup'
        }
    }
    if ($backend -and !$backend.HasExited) {
        try { Stop-Backend } catch { $backend.Kill(); Write-Warning $_ }
    }
    $env:MYSQL_PWD = $savedPassword
    Write-Output "Logs and scratch data: $Scratch"
}
