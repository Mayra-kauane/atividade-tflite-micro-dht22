# Modelos e métricas

Esta pasta guarda os artefatos gerados para documentar o fluxo do extra.

- `comfort_model.json`: modelo leve treinado a partir de `data/dht_comfort_dataset.csv`.
- `metrics.json`: métricas do modelo do extra e referência ao modelo Hello World embarcado.

O arquivo usado diretamente pelo firmware é gerado em:

```text
main/generated/comfort_model.h
```

Para regenerar os artefatos:

```powershell
python scripts\train_models.py
```
