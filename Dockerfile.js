FROM node:20-alpine
WORKDIR /app
COPY js/ ./js/
RUN cd js && npm install --omit=dev 2>/dev/null || true
ENV NODE_ENV=production
ENV LOG_LEVEL=info
ENV TELEGRAM_TOKEN=""
ENV TELEGRAM_CHAT_ID=""
CMD ["node", "js/src/main.js"]
