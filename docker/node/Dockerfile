FROM ubuntu:24.04 AS builder

ARG COMPILER=gcc
ARG NANO_NETWORK=live

# Install build dependencies
COPY ./ci/prepare/linux /tmp/prepare
RUN /tmp/prepare/prepare.sh

COPY ./ /tmp/src
WORKDIR /tmp/src

# Define ARGs for ci/build-node.sh
ARG BUILD_TYPE=RelWithDebInfo
ARG NANO_TEST=OFF
ARG NANO_TRACING=OFF
ARG COVERAGE=OFF
ARG CMAKE_SANITIZER=""
ARG CI_TAG=DEV_BUILD
ARG CI_VERSION_PRE_RELEASE=OFF
ARG SANITIZER

# Build node
RUN ci/build-node.sh
RUN echo ${NANO_NETWORK} >/etc/nano-network

FROM ubuntu:24.04

# Create nanocurrency user with UID/GID 1000, handling base images where 1000 is already taken
RUN getent group 1000 >/dev/null || groupadd --gid 1000 nanocurrency && \
    user_1000="$(getent passwd 1000 | cut -d: -f1 || true)" && \
    [ -z "$user_1000" ] && useradd --uid 1000 --gid 1000 --shell /bin/bash --create-home nanocurrency || \
    [ "$user_1000" = nanocurrency ] || usermod --login nanocurrency --home /home/nanocurrency --move-home "$user_1000"

COPY --from=builder /tmp/src/build/nano_node /usr/bin
COPY --from=builder /tmp/src/build/nano_rpc /usr/bin
COPY --from=builder /tmp/src/api/ /usr/bin/api/
COPY --from=builder /etc/nano-network /etc

COPY docker/node/entry.sh /usr/bin/entry.sh
COPY docker/node/config /usr/share/nano/config
RUN chmod +x /usr/bin/entry.sh
RUN ln -s /usr/bin/nano_node /usr/bin/rai_node
RUN ldconfig

WORKDIR /root
USER root

ENV PATH="${PATH}:/usr/bin"
ENTRYPOINT ["/usr/bin/entry.sh"]
CMD ["daemon"]

ARG REPOSITORY=nanocurrency/nano-node
LABEL org.opencontainers.image.source=https://github.com/$REPOSITORY