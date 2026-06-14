# Déploiement bmu.saillant.cc

Page de supervision publique (lecture seule) du BMU.

## Architecture
```
Navigateur ──https──▶ Cloudflare ──▶ nginx public (hôte saillant.cc)
                                       ├─ /            → page statique (web/)
                                       └─ /api/ (GET)  → proxy Tailscale ─▶ VM kxkm-bmu:8400
                                                          (+ Authorization: Bearer <clé>)
```
La clé API reste côté nginx ; le navigateur ne la voit jamais. Tout est servi
sous `bmu.saillant.cc` → pas de CORS.

## Prérequis
1. **Tailscale sur la VM** : `tailscale up` exécuté sur kxkm-bmu (10.2.5.110).
   Récupérer l'IP : `tailscale ip -4` (ou nom MagicDNS `kxkm-bmu`).
2. **Connectivité** : l'hôte nginx public doit être dans le même tailnet.
3. **DNS** : enregistrement `bmu.saillant.cc` → hôte nginx (Cloudflare, proxied pour le TLS).

## Étapes
```bash
# 1. Backend : redéployer l'API (nouveaux endpoints climate/solar) sur la VM
#    (depuis le repo, via le jump host — cf. project_kxkm_bmu_backend_vm)
#    rsync/tar kxkm-api/ → VM puis : docker compose up -d --build bmu-api

# 2. Page statique → hôte nginx public
sudo mkdir -p /var/www/bmu.saillant.cc
sudo cp kxkm-api/web/index.html /var/www/bmu.saillant.cc/

# 3. Vhost nginx
sudo cp kxkm-api/deploy/bmu.saillant.cc.nginx.conf /etc/nginx/sites-available/bmu.saillant.cc
#    éditer : <VM_TS_ADDR> = IP Tailscale de la VM, <BMU_API_KEY> = clé de ~/kxkm-api/.env
sudo ln -sf /etc/nginx/sites-available/bmu.saillant.cc /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl reload nginx
```

## Vérification
```bash
curl -s https://bmu.saillant.cc/api/bmu/batteries | head    # via le proxy (clé injectée)
# la page : https://bmu.saillant.cc
```

## Endpoints exposés (GET, lecture seule via proxy)
- `/api/bmu/batteries` — état courant des batteries + parc
- `/api/bmu/climate` · `/api/bmu/climate/history?from=-24h`
- `/api/bmu/solar` · `/api/bmu/solar/history?from=-24h`
- `/api/bmu/history?from=-24h[&battery=N]` — séries batteries
- `/api/bmu/audit` — journal (si exposé)
