FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates \
    libcurl4-openssl-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY src/ src/
COPY tests/ tests/

RUN mkdir build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
    && make -j$(nproc)

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libcurl4 zlib1g ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/meshtile /usr/local/bin/meshtile

ENV HOME=/data
VOLUME /data/.cache

EXPOSE 8080

ENTRYPOINT ["meshtile"]
CMD ["--region", "den"]
