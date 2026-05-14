# Relatório breve - Atividade Prática 4/6

## Objetivo

O objetivo da atividade foi reproduzir o exemplo **Hello World** do TensorFlow Lite Micro, executar no ESP32-S3 pelo Wokwi e documentar as observações encontradas. Como extra, o projeto foi modificado para usar um sensor DHT22 e um dataset próprio de conforto térmico.

## Evidência da execução

![Wokwi rodando Hello World e extra com DHT22](../img/esp32s3-tflite-micro-ashrae.png)

## Checklist

| Requisito | Evidência |
| --- | --- |
| Reproduzir os passos do Hello World | O firmware executa um modelo TFLite Micro que estima `sin(x)`. |
| Print do Wokwi rodando o Hello World | A imagem acima mostra o Wokwi com o circuito e o terminal serial. |
| Análise do código e documentação | Este relatório e o `README.md` documentam o funcionamento. |
| Extra com novo sensor e dataset | O extra usa DHT22, `data/dht_comfort_dataset.csv`, `models/comfort_model.json` e `models/metrics.json`. |

## Análise do Hello World

O modelo Hello World foi embarcado no arquivo `main/model.cc` como um array C/C++. Esse formato é necessário porque o ESP32-S3, nesse tipo de aplicação embarcada, não carrega o `.tflite` por sistema de arquivos durante a execução.

No código principal, `main/atividade_main.cc`, o firmware inicializa o TensorFlow Lite Micro com `tflite::InitializeTarget()`, carrega o modelo com `tflite::GetModel()` e valida a versão com `TFLITE_SCHEMA_VERSION`. Essa verificação evita executar um modelo incompatível com a versão da biblioteca.

O interpretador usa uma `tensor_arena` estática. Isso é uma característica importante do TensorFlow Lite Micro: a memória dos tensores é reservada previamente, o que combina melhor com microcontroladores.

O modelo usa entrada e saída quantizadas em `int8`. Por isso, antes da inferência, o valor `x` é convertido usando os parâmetros `scale` e `zero_point` do tensor de entrada. Depois da inferência, a saída é convertida novamente para `float`.

Exemplo de saída:

```text
I (...) atividade_4: Hello World | x=3.456 | predicted_sin=-0.322 | expected_sin=-0.309
```

## Análise do extra

O extra usa um sensor **DHT22** simulado no Wokwi. A leitura fornece temperatura e umidade, que são usadas para classificar o conforto térmico.

O dataset está em `data/dht_comfort_dataset.csv` e possui exemplos classificados como `frio`, `agradavel` e `quente`. O script `scripts/train_models.py` calcula um modelo supervisionado leve por centroides, gera `models/comfort_model.json`, grava métricas em `models/metrics.json` e atualiza `main/generated/comfort_model.h`, usado pelo firmware.

Essa escolha mantém o extra simples e adequado ao tamanho da atividade, mas ainda demonstra o ciclo sensor + dataset + treino/geração de modelo + decisão embarcada.

Métrica gerada para o extra:

```text
Acurácia: 100% no dataset local
Amostras: 22
Classes: frio, agradavel, quente
```

Exemplo de saída:

```text
I (...) atividade_4: Extra DHT22 | temp=25.0 C | umidade=60.0 % | conforto=agradavel
```

## Observações encontradas

- O Hello World é uma boa validação do fluxo de TinyML: treino, conversão, quantização e inferência embarcada.
- A quantização `int8` exige tratar `scale` e `zero_point` manualmente no firmware.
- A `tensor_arena` precisa ter tamanho suficiente para o modelo, mas não deve ser exagerada em microcontroladores.
- O Wokwi permite validar o circuito e o terminal serial sem placa física.
- O extra mostra uma aplicação prática de IoT: ler sensor, usar um dataset local e classificar uma condição do ambiente.
- A pasta `models/` ajuda a documentar o treinamento/geração do extra, de forma parecida com um fluxo de TinyML completo.

## Conclusão

O projeto atende aos itens principais da atividade e adiciona uma aplicação extra com DHT22 e dataset próprio. A saída no terminal mostra, em sequência, a inferência do Hello World e a classificação de conforto térmico baseada nas leituras do sensor.
