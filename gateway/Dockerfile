FROM python:3.12-slim
WORKDIR /app
COPY pyproject.toml ./
COPY palmfedi_gateway ./palmfedi_gateway
RUN pip install --no-cache-dir .
VOLUME /data
EXPOSE 8080
ENV PALMFEDI_DB=/data/palmfedi.sqlite3
CMD ["palmfedi-gateway", "--host", "0.0.0.0", "--port", "8080"]
