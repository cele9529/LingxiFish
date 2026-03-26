FROM node:18-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        mingw-w64 \
        curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY linggxi/package.json linggxi/package-lock.json ./

RUN npm ci --omit=dev

COPY linggxi/ .

RUN mkdir -p screenshots uploads temp

EXPOSE 8080 9090

CMD ["node", "app.js"]
