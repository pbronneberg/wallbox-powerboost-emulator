FROM mcr.microsoft.com/devcontainers/python:1-3.12-bookworm

RUN rm -f /etc/apt/sources.list.d/yarn.list /etc/apt/keyrings/yarn.gpg \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        curl \
        git \
        libffi-dev \
        libssl-dev \
        python3 \
        python3-dev \
        python3-pip \
        python3-venv \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspaces/wallbox-powerboost-emulator

COPY requirements.txt /tmp/wallbox-powerboost-emulator-requirements.txt
RUN python -m pip install --upgrade pip \
    && python -m pip install -r /tmp/wallbox-powerboost-emulator-requirements.txt
