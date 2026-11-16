<#
.SYNOPSIS
  Builds and (re)starts the Docker SFTP test server for the Sally SFTP plugin tests.
.DESCRIPTION
  Container 'sally-sftp-test': sshd on 127.0.0.1:2222 (with SFTP) and 127.0.0.1:2223 (without the SFTP subsystem), 2224 (keyboard-interactive only), 2225 (public key only) and 2226 (legacy algorithms only),
  user test/test. Test keys are generated once into tests/docker/out (git-ignored) and authorized in the container.
  Safe to run repeatedly; the container is recreated each time, existing keys are reused (delete out/ to regenerate).
#>
$ErrorActionPreference = 'Stop'

$image = 'sally-sftp-test'
$container = 'sally-sftp-test'
$here = $PSScriptRoot
$out = Join-Path $here 'out'
$passphrase = 'sally-test'

function Invoke-Docker {
    & docker @args
    if ($LASTEXITCODE -ne 0) { throw "docker $($args -join ' ') failed (exit code $LASTEXITCODE)" }
}

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) { throw 'docker was not found in PATH' }
cmd /c "docker info >nul 2>&1"
if ($LASTEXITCODE -ne 0) { throw 'The Docker daemon is not running' }

Invoke-Docker build -t $image $here

cmd /c "docker rm -f $container >nul 2>&1"
Invoke-Docker run -d --name $container -p 127.0.0.1:2222:2222 -p 127.0.0.1:2223:2223 -p 127.0.0.1:2224:2224 -p 127.0.0.1:2225:2225 -p 127.0.0.1:2226:2226 -p 127.0.0.1:2121:2121 -p 127.0.0.1:30000-30009:30000-30009 $image | Out-Null

# wait for sshd (the entrypoint first creates the fixtures)
$ready = $false
for ($i = 0; $i -lt 120 -and -not $ready; $i++) {
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $client.Connect('127.0.0.1', 2223)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 1000
        $buf = New-Object byte[] 4
        $ready = ($stream.Read($buf, 0, 4) -gt 0)   # sshd sends its "SSH-2.0" banner
    } catch { Start-Sleep -Milliseconds 500 } finally { $client.Close() }
}
if (-not $ready) { & docker logs $container; throw 'sshd did not start in time' }

# keys: generated inside the container (ssh-keygen and puttygen are there), kept in out/
New-Item -ItemType Directory -Force $out | Out-Null
if (-not (Test-Path (Join-Path $out 'id_ed25519'))) {
    $gen = @"
set -e
d=/tmp/keys
rm -rf `$d; mkdir -p `$d; cd `$d
ssh-keygen -q -t ed25519 -N '' -C sally-test -f id_ed25519
ssh-keygen -q -t rsa -b 3072 -N '' -C sally-test -f id_rsa
ssh-keygen -q -t ed25519 -N '$passphrase' -C sally-test -f id_ed25519_pass
echo '$passphrase' > pass.txt
for v in 2 3; do
  puttygen id_ed25519 -o ed25519_v`$v.ppk -O private --ppk-param version=`$v
  puttygen id_rsa -o rsa_v`$v.ppk -O private --ppk-param version=`$v
  puttygen id_ed25519_pass -o ed25519_pass_v`$v.ppk -O private --ppk-param version=`$v --old-passphrase pass.txt --new-passphrase pass.txt
done
rm pass.txt
"@
    Invoke-Docker exec $container sh -c ($gen -replace "`r", '')
    Invoke-Docker cp "${container}:/tmp/keys/." $out
    Invoke-Docker exec $container rm -rf /tmp/keys
    Set-Content -Path (Join-Path $out 'passphrase.txt') -Value $passphrase -NoNewline
}

# later additions (existing out/ folders get them too): passphrase-protected RSA and plain ECDSA nistp256 keys, as .ppk v2/v3
if (-not (Test-Path (Join-Path $out 'rsa_pass_v3.ppk'))) {
    $gen2 = @"
set -e
d=/tmp/keys2
rm -rf `$d; mkdir -p `$d; cd `$d
ssh-keygen -q -t rsa -b 3072 -N '$passphrase' -C sally-test -f id_rsa_pass
ssh-keygen -q -t ecdsa -b 256 -N '' -C sally-test -f id_ecdsa
echo '$passphrase' > pass.txt
for v in 2 3; do
  puttygen id_rsa_pass -o rsa_pass_v`$v.ppk -O private --ppk-param version=`$v --old-passphrase pass.txt --new-passphrase pass.txt
  puttygen id_ecdsa -o ecdsa_v`$v.ppk -O private --ppk-param version=`$v
done
rm pass.txt
"@
    Invoke-Docker exec $container sh -c ($gen2 -replace "`r", '')
    Invoke-Docker cp "${container}:/tmp/keys2/." $out
    Invoke-Docker exec $container rm -rf /tmp/keys2
}

# authorize the public keys of the plain and the passphrase-protected keys (rsa, ecdsa too)
$auth = (Get-Content (Join-Path $out 'id_ed25519.pub'), (Join-Path $out 'id_rsa.pub'), (Join-Path $out 'id_ed25519_pass.pub'), (Join-Path $out 'id_rsa_pass.pub'), (Join-Path $out 'id_ecdsa.pub')) -join "`n"
$auth | & docker exec -i $container sh -c 'cat > /home/test/.ssh/authorized_keys && chown test:test /home/test/.ssh/authorized_keys && chmod 600 /home/test/.ssh/authorized_keys'
if ($LASTEXITCODE -ne 0) { throw 'authorizing the keys failed' }

Write-Host "Plain FTP (for comparing with the FTP plugin): 127.0.0.1:2121, passive mode, user test / password test."
Write-Host "Test server ready: 127.0.0.1:2222 (SFTP), 2223 (no SFTP), 2224 (keyboard-interactive only), 2225 (public key only), 2226 (legacy algorithms only), user test / password test."
Write-Host "Keys: $out (passphrase of the protected keys: $passphrase)"
