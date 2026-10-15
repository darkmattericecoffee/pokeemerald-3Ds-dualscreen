# The plane-sharing server in a container: Python's standard library only, nothing to install.
#   docker compose up -d          (see docker-compose.yml and README.md, "Docker")
FROM python:3.12-slim
WORKDIR /app
COPY server.py db.py editfile.py sprites.py client.py ./
COPY static ./static
# The database and the exported game pictures live here (a volume); back up sprites.db.
VOLUME /data
EXPOSE 8787
# Inside the container it must listen on every interface; docker-compose.yml publishes the
# port on the host's 127.0.0.1 only, and the tunnel reaches it over the compose network.
CMD ["python", "server.py", "--data", "/data", "serve", "--host", "0.0.0.0", "--port", "8787"]
