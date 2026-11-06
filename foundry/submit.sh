#!/usr/bin/env bash
# Stage (git-tracked files only) and submit to Cloud Build in PAPPS. Detached (--async).
#   foundry/submit.sh firmware [ONLY="esphome idf ..."]   # the matrix
#   foundry/submit.sh images                              # rebuild all builder images
#   foundry/submit.sh image nuttx                         # rebuild one (tag from cloudbuild-images.yaml)
# Needs gcloud logged in as an account with Cloud Build Editor on papps-513907.
# If gcloud is not on this machine, set GCLOUD_HOST=cachy: the staged context is
# copied there and submitted from there.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PROJECT="${PROJECT:-papps-513907}"; BUCKET="${BUCKET:-papps-c-foundry}"
WHAT="${1:-firmware}"; ONLY="${2:-esphome idf arduino nuttx qemu}"
CTX="${CTX:-/tmp/kaizen-foundry-ctx}"
"$HERE/stage.sh" "$CTX" >/dev/null
DATE="$(date +%F)"
if [ "$WHAT" = images ]; then CFG=cloudbuild-images.yaml; SUBS=""; DIR="$CTX/foundry"
elif [ "$WHAT" = image ]; then
  N="$2"; U=$(echo "$N" | tr '[:lower:]' '[:upper:]')
  TAG=$(awk -v k="_${U}_TAG:" '$1==k {print $2}' "$HERE/cloudbuild-images.yaml")
  DIR="$CTX/foundry/images/$N"; CFG=""; SUBS=""
  IMG="us-central1-docker.pkg.dev/$PROJECT/kaizen-foundry/$N:$TAG"
else CFG=foundry/cloudbuild.yaml; SUBS="--substitutions=_DATE=$DATE,_ONLY=$ONLY"; DIR="$CTX"; fi
if [ -n "$CFG" ]; then TGT=(--config "$CFG"); else TGT=(--tag "$IMG" --machine-type e2-highcpu-8); fi
cmd=(builds submit . "${TGT[@]}" --project "$PROJECT" --billing-project "$PROJECT"
     --gcs-source-staging-dir "gs://$BUCKET/_source" --async)
if [ -n "${GCLOUD_HOST:-}" ]; then
  COPYFILE_DISABLE=1 tar --no-xattrs --exclude "._*" -C "$DIR" -czf /tmp/kaizen-foundry-ctx.tgz .
  scp -q /tmp/kaizen-foundry-ctx.tgz "$GCLOUD_HOST:/tmp/"
  # shellcheck disable=SC2029
  ssh "$GCLOUD_HOST" "bash -c 'rm -rf /tmp/kfctx && mkdir /tmp/kfctx && tar -C /tmp/kfctx -xzf /tmp/kaizen-foundry-ctx.tgz && cd /tmp/kfctx && \
    \${GCLOUD:-~/google-cloud-sdk/bin/gcloud} ${cmd[*]} ${SUBS:+\"$SUBS\"}'"
else
  cd "$DIR" && gcloud "${cmd[@]}" ${SUBS:+"$SUBS"}
fi
