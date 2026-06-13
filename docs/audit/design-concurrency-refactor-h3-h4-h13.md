# Note de conception — refactor concurrence hotplug (H3 / H4 / H13)

> Statut : **conception** (aucun code). Implémentation à réaliser **avec le matériel au banc** (ajout/retrait à chaud de modules INA237 / TCA9535). Réf. audit : `docs/audit/firmware-safety-audit-2026-06-13.md`.

## 1. Problème

Deux tâches FreeRTOS partagent l'état de topologie et les tableaux de devices I2C :

- **Tâche protection** (prio 8) — lit en continu `ctx->ina_devices[idx]` / `ctx->tca_devices[idx]`, `ctx->nb_ina`, `ctx->nb_tca`, et écrit l'état batterie sous `state_mutex`.
- **Tâche hotplug** (prio 3) — détecte les modules ajoutés/retirés et **mute directement** les mêmes tableaux.

Trois défauts en découlent :

| # | Défaut | Détail |
|---|--------|--------|
| **H3** | **use-after-free** | `remove_gone_ina/tca` font `i2c_master_bus_rm_device()` puis `memmove` (compaction) des structs `bmu_ina237_t`/`bmu_tca9535_handle_t` pendant que la tâche protection peut lire `&ctx->ina_devices[idx]` dans `bmu_ina237_read_voltage_current`. Le handle peut être supprimé/déplacé sous les pieds de la protection. `nb_ina_mutex` ne protège que le **compteur**, pas le contenu du tableau ni les handles. |
| **H4** | **écritures non protégées** | `*s_cfg.nb_tca` (l.189/277) et `*s_cfg.topology_ok` (l.359) sont écrits **sans mutex**, alors que `publish_snapshot` les lit (`nb_tca*4==nb_ina`). Lecture/écriture concurrente → lecture déchirée d'un drapeau qui pilote le fail-safe. |
| **H13** | **double source de vérité** | `nb_ina` existe en deux exemplaires : le `nb_ina` global de `main` (pointé par `ctx->nb_ina` côté cloud) et `prot->nb_ina`. Le hotplug met à jour les deux séparément ; la tâche cloud lit l'un avec un **fallback non protégé** si le mutex échoue. |

**Cause racine commune** : *deux écrivains* mutent un état partagé via *pointeurs directs*, avec une granularité de verrou incohérente (mutex sur le compteur, pas sur le contenu ; mutex différents entre écrivain et lecteur). C'est pourquoi un correctif partiel (ex. juste un mutex autour de `nb_tca`) donnerait une fausse assurance : il ne couvre pas la compaction des handles (H3) ni l'unification des sources (H13).

## 2. Principe de la solution : propriétaire unique + queue de commandes

**Un seul propriétaire mute le contexte de topologie : la tâche protection.** Le hotplug ne fait que *détecter* et *notifier*.

```
  Tâche hotplug (prio 3)                 Tâche protection (prio 8)
  ─────────────────────                  ─────────────────────────
  scan I2C (probe add/remove)
  construit un "set cible"   ──CMD_TOPOLOGY_SET──▶  applique TOUTES les
  (adresses présentes)         (queue)              mutations sous state_mutex :
  NE TOUCHE PLUS aux                                - add/remove device
  tableaux partagés                                 - compaction tableaux
                                                    - nb_ina / nb_tca / topology_ok
```

- Le hotplug n'appelle plus `bmu_protection_update_topology()` directement (l'appel direct ET la CMD coexistent aujourd'hui — double application, M2 de l'audit). **Un seul chemin : la queue.**
- La compaction des tableaux `ina_devices[]` / `tca_devices[]` et les `i2c_master_bus_rm_device()` se font **dans la tâche protection**, donc jamais concurremment avec une lecture de handle par cette même tâche → H3 éliminé.
- `nb_ina`, `nb_tca`, `topology_ok` deviennent des champs du **seul** `ctx`, écrits/lus sous `state_mutex` → H4 + H13 éliminés. Le `nb_ina` global de `main` est supprimé ; tout le monde lit `prot->nb_ina` via un accesseur mutexé `bmu_protection_get_nb_ina()`.

### Payload de la commande
`CMD_TOPOLOGY_SET` doit décrire l'état I2C cible de façon autoportante (pas de pointeur vers un tableau muté par le hotplug). Deux options :
- **(A) bitmap d'adresses présentes** : `uint16_t ina_present_mask`, `uint8_t tca_present_mask` + adresses de base. La protection diff par rapport à son état courant et applique add/remove. Compact, pas d'allocation.
- **(B) snapshot complet** : tableau d'adresses (≤ BMU_MAX_BATTERIES). Plus simple à raisonner, payload plus gros mais borné.

Recommandation : **(A) bitmap** — minimal, sans allocation, idempotent (le hotplug peut renvoyer l'état complet à chaque scan, la protection ne fait rien si pas de delta).

### Création/suppression des devices I2C
`i2c_master_bus_add_device` / `_rm_device` ne sont **pas** garantis thread-safe vis-à-vis des transactions en cours. En les déplaçant dans la tâche protection (qui est aussi celle qui fait les transactions), on sérialise naturellement add/remove et lecture — plus besoin de verrou inter-tâches sur le bus pour ce cas.

## 3. Étapes d'implémentation (incrémentales, build entre chaque)

1. **Accesseur unique `nb_ina`** : ajouter `bmu_protection_get_nb_ina()` (sous `state_mutex`), migrer tous les lecteurs (`cloud_telemetry_task`, BLE, VRM, balancer) dessus. Supprimer le `nb_ina` global de `main` et le fallback non protégé. *(H13)*
2. **Champs topologie dans `ctx`** : déplacer `nb_tca` / `topology_ok` dans `ctx`, accès sous `state_mutex` partout (y compris `publish_snapshot` et le gate C2 déjà en place). *(H4)*
3. **Commande unique** : remplacer l'appel direct `bmu_protection_update_topology()` du hotplug par l'unique envoi `CMD_TOPOLOGY_SET` (bitmap). Supprimer la double application. *(M2)*
4. **Déplacer la compaction** : implémenter dans la tâche protection un `apply_topology(mask)` qui fait add/remove device + compaction + maj compteurs, sous `state_mutex`. Le hotplug ne mute plus aucun tableau. *(H3)*
5. **Nettoyage** : retirer `nb_ina_mutex` (remplacé par `state_mutex` comme verrou unique de topologie) ou le renommer `topology_mutex` et l'utiliser de façon cohérente.

## 4. Plan de validation (banc matériel requis)

- **Fonctionnel** : retirer puis réinsérer à chaud un module INA237, puis un TCA9535 ; vérifier `nb_ina`/`nb_tca`/`topology_ok` cohérents, pas de batterie « fantôme », fail-safe sur mismatch.
- **Stress** : insertions/retraits répétés sous charge (batteries connectées, balancer actif) ; surveiller l'absence de crash (use-after-free), de `LoadProhibited`, de WDT.
- **Concurrence** : logs horodatés protection vs hotplug ; confirmer qu'aucune mutation de tableau n'a lieu hors tâche protection.
- **Non-régression** : 14 tests host + `qa-kxkm-s3-*` verts ; `idf.py build` OK ; budget mémoire < 85 %.
- **Durée** : prévoir un run d'endurance (≥ 1 h) hotplug aléatoire.

## 5. Risques & points d'attention

- **Latence topologie** : un module retiré n'est « vu » qu'au prochain cycle protection (≤ période boucle). Acceptable (le fail-safe coupe déjà sur lecture I2C en échec).
- **Taille de queue** : `CMD_TOPOLOGY_SET` est idempotent et écrasable → utiliser `xQueueOverwrite` sur une file de profondeur 1 dédiée, pour ne pas saturer la file de commandes générale.
- **Ordre d'init** : au boot, la topologie initiale doit être posée avant le démarrage de la tâche hotplug pour éviter une première CMD redondante.
- **Compat tests** : les tests `test_protection` / `test_snapshot` devront simuler `apply_topology` ; prévoir un point d'entrée testable en host.

## 6. Effort estimé

Moyen. ~1-2 j de dev + **0,5-1 j de validation au banc** (indispensable : ces bugs ne se reproduisent qu'avec du hotplug réel). À faire en un PR dédié, séparé de la remédiation déjà mergée.
