ARG BASE_IMAGE=ubuntu:22.04
FROM ${BASE_IMAGE}

WORKDIR /workspace

ENTRYPOINT ["/workspace/docker/entrypoint.sh"]