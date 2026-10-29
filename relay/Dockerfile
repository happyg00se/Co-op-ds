# DSCoop relay — tiny image, no dependencies beyond the Python stdlib.
FROM python:3.12-slim
LABEL org.opencontainers.image.title="dscoop-relay" \
      org.opencontainers.image.description="DSCoop relay/hub: room codes + UDP relaying for Death Stranding DC co-op"
RUN useradd -r -u 10001 dsrelay
WORKDIR /app
COPY relay.py /app/relay.py
USER dsrelay
# control port + 900 room ports (100 rooms of 8 players)
EXPOSE 27000/udp 27100-27999/udp
HEALTHCHECK --interval=30s --timeout=3s CMD python3 -c "import socket;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.settimeout(2);s.sendto(b'DSCP\x01\x09',('127.0.0.1',27000));s.recv(64)" || exit 1
ENTRYPOINT ["python3", "/app/relay.py"]
CMD ["--control-port", "27000", "--port-range", "27100-27999", "--stats-interval", "300"]
