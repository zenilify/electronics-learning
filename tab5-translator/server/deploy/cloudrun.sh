#!/usr/bin/env bash
# Deploy PAPP to Cloud Run (espbuilder / us-central1). Run on cachy from server/.
# Idempotent: re-running updates the image / secret version / service in place.
# Secrets: the one Secret Manager secret `papp-l4` holds "API_KEY=...\nPAPP_TOKEN=..."
# composed in-process from the two 0600 files and piped on stdin (never argv, never echoed).
set -euo pipefail
G=${GCLOUD:-$HOME/google-cloud-sdk/bin/gcloud}
# docker's credHelper `docker-credential-gcloud` must be on PATH (bash -lc on cachy lacks it;
# the fish login shell has it) — measured 2026-11-07.
export PATH="$(dirname "$G"):$PATH"
P=espbuilder; R=us-central1; REPO=papp; SVC=papp; SECRET=papp-l4
SA=papp-run@$P.iam.gserviceaccount.com
IMG=$R-docker.pkg.dev/$P/$REPO/papp:${TAG:-$(date +%Y%m%d-%H%M)}
LABELS=lane=l4,owner=erick
step=${1:-all}

if [[ $step == all || $step == infra ]]; then
  $G artifacts repositories describe $REPO --location $R --project $P >/dev/null 2>&1 ||
    $G artifacts repositories create $REPO --repository-format docker --location $R \
      --project $P --labels $LABELS --description "PAPP translator (lane L4)" --quiet
  $G iam service-accounts describe $SA --project $P >/dev/null 2>&1 ||
    $G iam service-accounts create papp-run --project $P --display-name "PAPP Cloud Run (L4)" --quiet
  if ! $G secrets describe $SECRET --project $P >/dev/null 2>&1; then
    $G secrets create $SECRET --project $P --replication-policy automatic --labels $LABELS --quiet
  fi
  # LANDMINE 312: grant only what Cloud Run needs — accessor on this one secret.
  $G secrets add-iam-policy-binding $SECRET --project $P --member serviceAccount:$SA \
    --role roles/secretmanager.secretAccessor --quiet >/dev/null
fi

if [[ $step == all || $step == secret ]]; then
  python3 - <<'PY' | $G secrets versions add $SECRET --project $P --data-file=- --quiet
import os
d = os.path.expanduser("~/.config/zenilify/")
key = [l.split("=", 1)[1].strip() for l in open(d + "nvidia_build.env") if l.startswith("API_KEY=")][0]
tok = open(d + "papp_token").read().strip()
print(f"API_KEY={key}\nPAPP_TOKEN={tok}")
PY
fi

if [[ $step == all || $step == image ]]; then
  $G auth configure-docker $R-docker.pkg.dev --quiet >/dev/null
  docker build -t "$IMG" . && docker push "$IMG"
  echo "$IMG" > /tmp/papp_last_image
fi

if [[ $step == all || $step == deploy ]]; then
  IMG=${IMG_OVERRIDE:-$(cat /tmp/papp_last_image)}
  $G run deploy $SVC --project $P --region $R --image "$IMG" --service-account $SA \
    --min-instances 0 --max-instances 1 --concurrency 4 --cpu 1 --memory 512Mi \
    --timeout 120 --cpu-throttling --no-cpu-boost --execution-environment gen2 \
    --set-secrets /secrets/papp/papp.env=$SECRET:latest \
    --set-env-vars PAPP_SECRETS_FILE=/secrets/papp/papp.env \
    --labels $LABELS --allow-unauthenticated --quiet
  $G run services describe $SVC --project $P --region $R --format 'value(status.url)'
fi
