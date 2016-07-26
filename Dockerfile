FROM ubuntu:16.04
MAINTAINER baoshan

# build environment settings
ENV instsubmitcgi="false"

# install uwsgi
RUN \
  apt-get update -q && \
  apt-get install -y \
    curl \
    python-pip \
    uwsgi && \

# install cddbd
  mkdir -p /usr/local/cddbd/cgi && cd /usr/local/cddbd && \
  curl -O http://ftp.freedb.org/pub/freedb/cddbd-1.5.2.tar.gz && \
  tar -xzvf cddbd-1.5.2.tar.gz && rm cddbd-1.5.2.tar.gz && \
  cd cddbd-1.5.2 && \
  echo | ./config.sh && \
  make && \
  /bin/echo -e "\n\n\n\n\n/usr/local/cddbd/cgi\nn\n\n\n\n\n\n\n\n\n\n\n\n\n\nn\nn" | ./install.sh && \
  cd .. && rm -rf cddbd-1.5.2 && \

# cleanup
  apt-get purge -y \
    curl \
    python-pip && \
  apt-get clean && \
  rm -rf \
    /tmp/* \
    /var/lib/apt/lists/* \
    /var/tmp/*

COPY cddbd.ini /usr/local/cddbd
COPY start /usr/local/cddbd

ENTRYPOINT ["/usr/local/cddbd/start"]

EXPOSE 8080
