# Cloud Stack (kxkm-ai)

Docker stack hébergé sur le serveur `kxkm-ai` (Tailscale `100.87.54.119`). MQTT broker, time-series, REST API, dashboards.

## Deployment

```bash
ssh kxkm@kxkm-ai
cd ~/kxkm-api
docker compose up -d                    # démarrer
docker compose logs -f kxkm-mosquitto   # logs d'un service
docker ps --format 'table {{.Names}}\t{{.Status}}' | grep kxkm-
```

## Services

| Service | Port | Rôle |
|---------|-----:|------|
| `kxkm-mosquitto` | 1883, 9001 (WS) | MQTT broker authentifié |
| `kxkm-influxdb` | 8086 | Time-series (org=kxkm, bucket=bmu) |
| `kxkm-telegraf` | - | Bridge MQTT → InfluxDB |
| `kxkm-bmu-api` | 8400 | FastAPI REST (sync, history, audit) |
| `kxkm-grafana` | 3001 | 3 dashboards (live, fleet, solar) |

## Secrets (`.env`, chmod 600)

Tous les secrets passent par variables d'environnement :
- `BMU_API_KEY` : clé API FastAPI (32+ chars)
- `INFLUX_TOKEN` : token InfluxDB (rotaté)
- `MQTT_USERNAME` / `MQTT_PASSWORD` : auth Mosquitto
- `GF_ADMIN_PASSWORD` : password Grafana admin
- `CORS_ORIGINS` : origines autorisées (séparées par virgule)

**Ne jamais commit `.env`** — utiliser `.env.example` comme template.

## Mosquitto Auth

`allow_anonymous false` + password file `/mosquitto/config/passwd`. Users : `bmu` (firmware), `telegraf` (bridge). Créer un user :
```bash
docker exec kxkm-mosquitto mosquitto_passwd -b /mosquitto/data/passwd <user> <password>
docker cp kxkm-mosquitto:/mosquitto/data/passwd ./mosquitto/passwd
docker compose restart mosquitto
```

## Telegraf Topics

```
bmu/+/battery/#    # multi-BMU (bmu/<name>/battery/<idx>)
bmu/battery/#      # legacy single-BMU
```

## Anti-Patterns

- Ne pas hardcoder de secrets dans `docker-compose.yml`
- Ne pas désactiver CORS (`allow_origins=["*"]`) — restreindre via `.env`
- Ne pas utiliser le token InfluxDB par défaut (`kxkm-influx-token-2026`) — rotater
- Ne pas exposer les ports en clair sans Tailscale ou reverse proxy TLS
