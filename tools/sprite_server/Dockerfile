# The plane-sharing server in a container: the web app is built in a Node stage, the server
# itself is Python's standard library only.
#   docker compose up -d          (see docker-compose.yml and README.md, "Docker")

# 1. The web app (web/): Vite builds it into static files.
FROM node:22-alpine AS web
WORKDIR /web
COPY web/package.json web/package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY web/ ./
RUN npm run build

# 2. The server, serving the built app from web/dist.
FROM python:3.12-slim
WORKDIR /app
COPY server.py db.py events.py signing.py editfile.py sprites.py client.py ./
COPY --from=web /web/dist ./web/dist
# The database and the exported game pictures live here (a volume); back up sprites.db.
VOLUME /data
EXPOSE 8787
# Inside the container it must listen on every interface; docker-compose.yml publishes the
# port on the host's 127.0.0.1 only, and the tunnel reaches it over the compose network.
CMD ["python", "server.py", "--data", "/data", "serve", "--host", "0.0.0.0", "--port", "8787"]
