---
description: "Use when writing or modifying Python tests in firmware/test/*.py or firmware/test/**. Enforce deterministic pytest style, clear evidence, and CI-friendly assertions."
name: "Python Test Rules"
applyTo: ["firmware/test/**/*.py", "**/firmware/test/**/*.py"]
---

# Python Test Rules

- Framework: utiliser pytest.
- Nommage: fichiers test_*.py, fonctions test_*.
- Structure: Arrange / Act / Assert, un scénario principal par test.
- Déterminisme: pas d'accès réseau, pas de dépendance horloge non contrôlée, pas d'aléatoire sans seed.
- I/O: utiliser tmp_path et fixtures, ne pas écrire hors répertoire temporaire.
- Assertions: messages explicites sur les écarts attendus.
- Paramétrage: préférer @pytest.mark.parametrize pour les variations d'entrée.
- Evidence: chaque test doit valider un comportement observable et vérifiable.
- Gates: ajouter au moins un cas nominal, un cas limite, un cas erreur.
- Performance CI: éviter les tests lents; isoler les tests lourds avec un marqueur explicite.