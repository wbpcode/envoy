FROM node:20.2-bullseye-slim@sha256:afbc0ebb3e5de0a3dfc892ddc46fc18f0c40f5b3f0afb2692ec732e2b64bc0aa as node-base


FROM node-base as node-http-auth
ARG NODE_APP=.
ARG NODE_APP_NAME=http-service/server
# Add an env to save ARG
ENV NODE_APP_PATH "/app/${NODE_APP_NAME}"
COPY "$NODE_APP" /app
# Dont use exec form to interpolate correctly
CMD node $NODE_APP_PATH
