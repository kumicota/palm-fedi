FROM python:3.12-slim
# docker build --build-arg WITH_FFMPEG=1 ... adds video previews
ARG WITH_FFMPEG=0
RUN if [ "$WITH_FFMPEG" = 1 ]; then apt-get update && \
    apt-get install -y --no-install-recommends ffmpeg && rm -rf /var/lib/apt/lists/*; fi
WORKDIR /app
COPY pyproject.toml ./
COPY palmfedi_gateway ./palmfedi_gateway
RUN pip install --no-cache-dir .
VOLUME /data
EXPOSE 8080
ENV PALMFEDI_DB=/data/palmfedi.sqlite3
CMD ["palmfedi-gateway", "--host", "0.0.0.0", "--port", "8080"]
