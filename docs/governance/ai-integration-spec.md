# Spec d'integration intelligence BMU

Date: 2026-03-30  
Statut: proposee  
Portee: firmware BMU, pipeline ML, cloud analytics, gouvernance QA

## 1. Objectif

Definir une integration d'intelligence exploitable en production pour le projet KXKM Batterie Parallelator, sans compromettre la surete locale.

Principes directeurs:
- La protection batterie reste 100% locale dans le firmware.
- Les sorties IA sont consultatives (aide decision), jamais autorite de coupure/commutation.
- Les modeles et metriques doivent etre reproductibles et tracables.
- Les budgets RAM/flash et les contraintes temps reel restent prioritaires.

## 2. Audit documentaire (etat au 2026-03-30)

### 2.1 Constats critiques

1. Integration IA edge non implementee dans le firmware.
- Constat: aucune classe ou tache d'inference SOH/RUL dans le code embarque.
- Evidence:
  - firmware/src/** sans symbole d'inference (SOH, RUL, FPNN, SambaMixer).
  - Le flux IA existe surtout dans scripts/ml/** et models/**.
- Risque: ecart entre promesse produit et execution runtime.

2. Incoherences de references et de perimetre dans la spec ML existante.
- Constat: la spec ML historique mentionne des hypotheses/chemins qui ne refletent plus toujours la structure migree.
- Evidence:
  - docs/ml-battery-health-spec.md mentionne des cibles runtime avancees non presentes dans firmware/src/.
  - Branch/process historiques cites differemment de l'etat actuel (main + migration firmware/hardware).
- Risque: confusion d'implementation et derive de priorites.

### 2.2 Constats majeurs

1. Gate qualite SOH insuffisamment formalise dans les docs de gouvernance.
- Constat: des metriques existent (models/phase2_metrics.json) mais les seuils de release IA ne sont pas centralises dans un contrat unique edge/cloud.
- Risque: validation non homogene entre equipe firmware, ML et QA.

2. Preuve CI distante non systematisee pour la chaine IA.
- Constat: execution locale bien documentee, mais la preuve distante (runs CI, artefacts) reste partielle dans les plans.
- Risque: difficulte a fermer les gates de livraison de facon auditable.

3. Couplage fonctionnel entre telemetrie et IA encore implicite.
- Constat: pas de schema de contrat de donnees versionne (features obligatoires, frequences, tolerances NaN/outliers, unite stricte).
- Risque: drift silencieux des entrees et degradation de la qualite modele.

### 2.3 Constats moderes

1. Vocabulaire capteur a harmoniser dans la documentation.
- Constat: references INA237/INA226 a clarifier selon le scope (materiel v2, driver local, docs historiques).
- Risque: confusion de maintenance et erreurs d'interpretation.

2. Strategie de deploiement edge detaillee mais pas contractualisee.
- Constat: budgets memoire et cadence inference discutes, mais sans tableau de conformite unique relie aux builds cibles.
- Risque: regressions de performance detectees tardivement.

## 3. Vision d'integration IA cible

### 3.1 Repartition des responsabilites

- Firmware local (autorite surete):
  - Lecture capteurs, protections V/I/desequilibre, logique switch.
  - Publication telemetrie et reception de recommandations IA non bloquantes.

- IA edge (advisory):
  - Estimation SOH locale periodique par batterie.
  - Detection d'anomalies soft (niveau information/alerte).
  - Jamais de commande directe de coupure/reconnexion.

- IA cloud (advisory avancee):
  - Prevision RUL long horizon, tendances flotte, calibration confiance.
  - Recommandations de maintenance et priorisation d'interventions.

### 3.2 Contrat de surete non negociable

1. Toute protection critique reste dans le chemin deterministe firmware.
2. En cas d'erreur IA (timeout, modele absent, sortie invalide), fallback immediat en mode telemetrie seule.
3. Les sorties IA doivent porter un champ de confiance et un statut qualite des donnees.
4. Aucune route web mutatrice ne doit accepter une decision IA comme autorisation implicite.

## 4. Spec fonctionnelle d'integration

### 4.1 Donnees d'entree et schema

Schema minimal par batterie (fenetre glissante):
- timestamp_ms
- voltage_v
- current_a
- switch_state
- ah_discharge
- ah_charge
- quality_flags (nan_count, stale_data, i2c_fault)

Features derivees minimales:
- V_mean, V_std
- I_mean, I_std
- dV_dt, dI_dt
- R_internal_est
- capacity_fade_proxy
- coulombic_efficiency (si disponible et qualite OK)

Regles schema:
- Versionnage obligatoire du schema features (ex: feature_schema_version).
- Unites explicites et fixes (V, A, Ah, s).
- Rejet explicite des fenetres invalides (qualite insuffisante).

### 4.2 Sorties IA

Sortie SOH edge (par batterie):
- soh_percent: float [0..100]
- confidence: float [0..1]
- model_version: string
- data_quality: enum {ok, degraded, invalid}
- advisory_level: enum {info, warning, critical}

Sortie RUL cloud (par batterie):
- rul_cycles
- rul_days_estimated
- confidence_interval_p10_p90
- drift_flag
- recommendation_text

### 4.3 Interfaces d'integration

Firmware -> cloud:
- Canal existant (Influx/MQTT/WebSocket) conserve.
- Ajout de champs IA sans casser la telemetrie actuelle.

Cloud -> supervision:
- Endpoint ou payload dedie pour vue flotte SOH/RUL.
- Journalisation des recommandations et de leur statut (appliquee/ignoree).

## 5. Spec non fonctionnelle

### 5.1 Contraintes edge

- Periodicite inference SOH: 30 a 120 s (configurable).
- Budget CPU inference: < 100 ms par batterie (cible initiale).
- Budget memoire IA edge:
  - Poids modele: < 50 KB (objectif < 33 KB).
  - Scratch + buffers: borne et mesure en build cible.
- Zero blocage des taches de protection et bus I2C.

### 5.2 Qualite modele et gates

Gates minimaux pour release IA:
1. SOH:
- MAPE in-domain <= 15% (minimum release)
- MAPE cible <= 5% (objectif produit)

2. Quantification:
- Degradation MAPE post-quantif <= 5 points
- Taille artefact quantifie conforme budget

3. RUL:
- Metrique explicite par split (device/time), pas uniquement globale
- Rapport d'incertitude obligatoire

4. Robustesse:
- Rapport drift (in-domain vs out-of-domain)
- Cas fallback verifies (donnees degradees, capteur indisponible)

### 5.3 Observabilite

- Logs structures pour inference (start/end/error/fallback) via logger projet.
- Compteurs runtime:
  - inference_ok_count
  - inference_fallback_count
  - invalid_window_count
  - model_load_error_count

## 6. Plan d'implementation recommande

Phase A - Alignement documentaire et contrat (court terme)
- A1: Harmoniser docs/ml-battery-health-spec.md avec cette spec.
- A2: Definir un contrat de donnees versionne (features + unites + qualite).
- A3: Ajouter section gates IA dans gouvernance QA.

Phase B - Integration firmware minimale (MVP advisory)
- B1: Introduire module SOHEstimator en mode advisory only.
- B2: Ajouter buffer fenetre glissante et validation qualite.
- B3: Publier sortie SOH + confiance sans action de controle.
- B4: Ajouter tests unitaires host pour parsing/validation sortie IA.

Phase C - Industrialisation cloud et CI
- C1: Pipeline CI distant pour entrainement/evaluation/artefacts.
- C2: Archivage automatique des metriques et model cards.
- C3: Dashboard supervision flotte (SOH/RUL + drift + confiance).

Phase D - Durcissement surete
- D1: Validation terrain longue duree.
- D2: Fault injection (NaN massifs, pertes capteurs, latence cloud).
- D3: Revue securite des routes web mutatrices post-integration.

## 7. Criteres d'acceptation

La spec est consideree integree lorsque:
1. Le firmware publie SOH advisory par batterie sans impact sur protections.
2. Les gates qualite IA sont automatisees en CI et tracees.
3. Les preuves (build, tests, metriques, artefacts) sont reliees a une release.
4. Les documents de gouvernance et plan sont synchronises.

## 8. Backlog priorise (issue-ready)

P0
- Definir contrat de donnees IA versionne (schema + unites + qualite).
- Integrer SOH advisory edge sans commande mutatrice.
- Ajouter test de non-regression surete avec IA active/inactive.

P1
- Standardiser gates IA dans pipeline distant.
- Ajouter rapport drift et confiance dans metriques finales.
- Harmoniser nomenclature capteurs dans docs techniques.

P2
- Ajouter recommandations maintenance contextualisees dans supervision.
- Ajouter benchmark energie/latence inference par cible MCU.

## 9. Documents relies

- README.md
- docs/ml-battery-health-spec.md
- docs/governance/integration.md
- docs/governance/feature-map.md
- plan/refactor-safety-core-web-remote-1.md
- firmware/src/main.cpp
- firmware/src/BatteryParallelator.cpp
- firmware/src/WebServerHandler.cpp
- scripts/ml/extract_features.py
- scripts/ml/train_fpnn.py
- scripts/ml/train_sambamixer.py
- models/phase2_metrics.json