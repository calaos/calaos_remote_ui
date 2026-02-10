FROM espressif/idf:release-v5.5

ARG DEBIAN_FRONTEND=nointeractive
ARG CONTAINER_USER=esp
ARG USER_UID=1000
ARG USER_GID=$USER_UID

RUN curl -fsSL https://deb.nodesource.com/setup_22.x | bash -

RUN apt-get update \
  && apt install -y -q \
  cmake iproute2 \
  git \
  hwdata \
  libglib2.0-0 \
  libnuma1 \
  libpixman-1-0 \
  linux-tools-virtual \
  sudo nodejs \
  build-essential \
  libzstd-dev \
  libmbedtls-dev \
  libcriterion-dev \
  meson gdb gdbserver \
  golang \
  cifs-utils \
  python3-pip \
  python3-png \
  python3-lz4 \
  libx11-dev \
  libsdl2-dev \
  libwebsockets-dev \
  && rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install --break-system-packages pypng
RUN python3 -m pip install --break-system-packages lz4

RUN sudo npm i lv_i18n -g
RUN sudo npm i lv_font_conv -g

ENV LC_ALL=C.UTF-8
ENV LANG=C.UTF-8

RUN mkdir -p /root/.ssh \
  && chmod 700 /root/.ssh \
  && touch /root/.ssh/known_hosts \
  && ssh-keyscan github.com >> /root/.ssh/known_hosts

RUN chmod -R 775 /opt/esp/python_env/

RUN echo "esp ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

RUN usermod -u 1001 ubuntu && groupmod -g 1001 ubuntu && \
    groupadd --gid $USER_GID $CONTAINER_USER && \
    adduser --uid $USER_UID --gid $USER_GID --disabled-password --gecos "" ${CONTAINER_USER} && \
    usermod -a -G dialout $CONTAINER_USER && \
    usermod -a -G sudo $CONTAINER_USER && \
    usermod -a -G root $CONTAINER_USER && \
    echo "esp:esp" | chpasswd

USER ${CONTAINER_USER}
ENV USER=${CONTAINER_USER}
WORKDIR /home/${CONTAINER_USER}

RUN echo "source /opt/esp/idf/export.sh > /dev/null 2>&1" >> ~/.bashrc

RUN IDF_PATH_FORCE=1 . /opt/esp/idf/export.sh IDF_PATH=/opt/esp/idf && \
    pip3 install pypng lz4

ENTRYPOINT [ "/opt/esp/entrypoint.sh" ]

CMD ["/bin/bash"]
