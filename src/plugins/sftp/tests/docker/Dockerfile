FROM debian:stable-slim

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends openssh-server putty-tools vsftpd \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /run/sshd \
    && useradd -m -s /bin/bash test \
    && echo 'test:test' | chpasswd

COPY sshd_config /etc/ssh/sshd_config
COPY sshd_config_nosftp /etc/ssh/sshd_config_nosftp
COPY sshd_config_kbdint /etc/ssh/sshd_config_kbdint
COPY sshd_config_pubkey /etc/ssh/sshd_config_pubkey
COPY sshd_config_legacy /etc/ssh/sshd_config_legacy
COPY vsftpd.conf /etc/vsftpd.conf
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

EXPOSE 2222 2223 2224 2225 2226 2121 30000-30009
ENTRYPOINT ["/entrypoint.sh"]
