# Minimal image for the fgtest FastAPI service
# Build: docker build -t symsan-web-service web-service
# Run (mount fgtest and data dirs):
#   docker run --rm -p 8000:8000 -v /path/to/fgtest:/opt/fgtest \
#     -v $PWD/uploads:/app/uploads -v $PWD/results:/app/results symsan-web-service
FROM python:3.11-slim
ENV DEBIAN_FRONTEND=noninteractive
ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    FGTEST_PATH=/opt/fgtest


WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY app.py fgtest_wrapper.py ./
COPY ./bin ./bin
# Ensure runtime directories exist
RUN mkdir -p uploads results

EXPOSE 8000
ENTRYPOINT ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "8000"]
