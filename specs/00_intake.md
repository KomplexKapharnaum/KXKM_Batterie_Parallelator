# Intake — KXKM Batterie Parallelator

> Kill_LIFE gate: S0 (intake)
> Date: 2026-03-25
> Client: KompleX KapharnaüM (Villeurbanne)
> Contact: Nicolas GUICHARD — nicolas.guichard@lehoop.fr (partenaire)
> Owner: clement@lelectronrare.fr

## Contexte

KompleX KapharnaüM déploie des installations de scénographie numérique en milieu urbain, hors réseau électrique. Plusieurs packs batterie 24–30 V sont mis en parallèle pour alimenter les dispositifs (LED, son, projection, contrôle). La mise en parallèle naïve de batteries à des états de charge différents génère des courants d'équilibrage dangereux.

## Besoin

Système embarqué de gestion sécurisée de batteries en parallèle :
- Surveillance individuelle tension / courant / puissance de chaque pack
- Protection : coupure automatique sur sur-tension, sous-tension, sur-courant, déséquilibre
- Reconnexion automatique après résolution d'un défaut (avec compteur de coupures)
- Supervision visuelle LED pour les techniciens terrain

## Contraintes

- Jusqu'à 16 packs batterie (24–30 V, max 1 A de déséquilibre admis)
- Matériel ESP32 (K32 board KXKM) + INA237 (mesure I²C) + TCA9535 (GPIO expander relais)
- Framework Arduino / PlatformIO
- Déploiement terrain : robustesse, auto-récupération
- PCB existant : BMU v1 (produit JLCPCB), BMU v2 (en cours)

## Périmètre L'Électron Rare

- Revue et consolidation du firmware existant
- Tests unitaires natifs (Kill_LIFE gate S1)
- Documentation technique et specs formelles
- Intégration CI/CD GitHub Actions
