# CONTRIBUTING

Danke fürs Mitbauen! Kurze Regeln:

## Branches & Commits
- `main` ist stabil. Feature-Branches: `feature/<kurzbeschreibung>`
- Kleine, fokussierte Commits mit verständlichen Messages.

## Issues
- Nutze die Templates (`Bug report`, `Feature request`).
- Reproduzierbarkeit ist König: Logs, UE-Version, Schritte.

## Pull Requests
- Bitte **kleine, reviewbare PRs**.
- Beschreibe **Was** & **Warum** im PR-Template.
- Keine neuen externen Abhängigkeiten ohne Diskussion.

## Stil
- **UE 5.6.1**-Kompatibilität sicherstellen.
- Kein VaRest – HTTP nur über `FHttpModule`.
- Audio: WAV bevorzugen für Playback in UE.
