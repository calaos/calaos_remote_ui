FROM espressif/idf:release-v5.5

ARG DEBIAN_FRONTEND=nointeractive

RUN curl -fsSL https://deb.nodesource.com/setup_22.x | bash -

RUN apt-get update \
  && apt install -y -q \
  cmake \
  git \
  nodejs \
  build-essential \
  libzstd-dev \
  libmbedtls-dev \
  python3-pip \
  python3-png \
  python3-lz4 \
  libx11-dev \
  libsdl2-dev \
  && rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install --break-system-packages pypng lz4

RUN IDF_PATH_FORCE=1 . /opt/esp/idf/export.sh IDF_PATH=/opt/esp/idf && \
    pip3 install pypng lz4

RUN npm i lv_i18n -g
RUN npm i lv_font_conv -g

ENV LC_ALL=C.UTF-8
ENV LANG=C.UTF-8

RUN mkdir -p /root/.ssh \
  && chmod 700 /root/.ssh \
  && touch /root/.ssh/known_hosts \
  && ssh-keyscan github.com >> /root/.ssh/known_hosts

ENTRYPOINT [ "/opt/esp/entrypoint.sh" ]

CMD ["/bin/bash"]
