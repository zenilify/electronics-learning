# PAPP translator backend. No secrets in any layer: the NVIDIA key and the bearer
# token are mounted at runtime (cachy: bind-mounted 0600 files; Cloud Run: Secret
# Manager volume at /secrets/papp/papp.env -> PAPP_SECRETS_FILE).
FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1 PIP_NO_CACHE_DIR=1 PORT=8080
WORKDIR /app
COPY requirements.txt .
RUN pip install -r requirements.txt
COPY papp/ papp/
# files edited through some sync paths arrive 0600; the app runs as uid 1000 -> make them readable
RUN chmod -R a+rX /app

# uid 1000 so a 0600 file owned by cachy (uid 1000) is readable when bind-mounted.
RUN useradd -u 1000 -m papp
USER papp
EXPOSE 8080
CMD ["sh", "-c", "exec uvicorn papp.app:app --host 0.0.0.0 --port ${PORT} --workers 1 --no-access-log"]
