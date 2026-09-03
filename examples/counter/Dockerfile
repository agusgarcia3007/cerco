# build stage: toolchain + cerco + app
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl clang lld llvm make git xz-utils \
 && rm -rf /var/lib/apt/lists/*
# install cerco (single binary; copy it into the context or fetch a release)
COPY cerco /usr/local/bin/cerco
RUN chmod +x /usr/local/bin/cerco
WORKDIR /app
COPY . .
RUN cerco build

# runtime stage: only the binary
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl \
 && rm -rf /var/lib/apt/lists/* \
 && useradd -r -u 10001 app
WORKDIR /srv
COPY --from=build /app/dist/my-app /srv/my-app
USER app
ENV HOST=0.0.0.0 PORT=3000
EXPOSE 3000
HEALTHCHECK --interval=10s --timeout=2s CMD curl -sf http://127.0.0.1:3000/__cerco/health || exit 1
ENTRYPOINT ["/srv/my-app"]
