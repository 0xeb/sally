#!/bin/sh
# Creates the test fixtures (once), runs a plain FTP server (vsftpd, 2121) and five sshd instances: 2222 (SFTP), 2223 (no SFTP), 2224 (keyboard-interactive only), 2225 (public key only), 2226 (legacy algorithms only).
set -e

ssh-keygen -A

HOME_DIR=/home/test
FIX=$HOME_DIR/fixtures

if [ ! -d "$FIX" ]; then
    mkdir -p "$FIX/small" "$FIX/nested/a/b/c" "$FIX/empty"
    echo "hello" > "$FIX/small/hello.txt"
    head -c 1024 /dev/urandom > "$FIX/small/random1k.bin"
    head -c 1048576 /dev/urandom > "$FIX/small/random1m.bin"
    echo "deep" > "$FIX/nested/a/b/c/deep.txt"
    ln -s nested "$FIX/dirlink"
    ln -s small/hello.txt "$FIX/filelink"
    ln -s missing-target "$FIX/brokenlink"
    # file name with the raw byte 0xE9 (Latin-1 e-acute), not valid UTF-8
    printf 'latin1\n' > "$FIX/$(printf 'caf\351.txt')"
    mkdir -p "$FIX/many"
    i=0
    while [ $i -lt 5000 ]; do
        : > "$FIX/many/file_$i.txt"
        i=$((i + 1))
    done
    # 5 GB sparse file, for 64-bit offsets
    truncate -s 5G "$FIX/sparse5g.bin"
    mkdir -p "$HOME_DIR/upload"
    chown -R test:test "$HOME_DIR"
fi

mkdir -p /run/sshd "$HOME_DIR/.ssh"
touch "$HOME_DIR/.ssh/authorized_keys"
chown -R test:test "$HOME_DIR/.ssh"
chmod 700 "$HOME_DIR/.ssh"
chmod 600 "$HOME_DIR/.ssh/authorized_keys"

mkdir -p /var/run/vsftpd/empty
/usr/sbin/vsftpd /etc/vsftpd.conf &
/usr/sbin/sshd -f /etc/ssh/sshd_config_nosftp
/usr/sbin/sshd -f /etc/ssh/sshd_config_kbdint
/usr/sbin/sshd -f /etc/ssh/sshd_config_pubkey
/usr/sbin/sshd -f /etc/ssh/sshd_config_legacy
exec /usr/sbin/sshd -D -e -f /etc/ssh/sshd_config
