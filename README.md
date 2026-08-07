<img width="1952" height="2172" alt="Gemini_Generated_Image_291gjv291gjv291g" src="https://github.com/user-attachments/assets/ab7c8556-62e1-490c-889f-321f204d4294" />

  ## 🎨 KenWoody, O Príncipe

  Pixel art desenhado combinando:

    • 👑 Coroa dourada com rubis e safiras no topo
    • 🤠 Chapéu do Woody debaixo da coroa
    • 💇 Cabelo loiro do Ken saindo do chapéu
    • 👀 Olhos azuis estilo Ken
    • 🔴 Lenço vermelho do Woody no pescoço
    • 👔 Camisa xadrez amarela/vermelha do Woody
    • 🦺 Colete de couro + ⭐ estrela de xerife
    • 💜 Capa roxa de príncipe nas costas
    • 👖 Calça jeans do Ken
    • 🥾 Botas de cowboy do Woody

  ## 📱 Fluxo de telas
  

  ### 📊 Monitor de Sinais (Osciloscópio) — 6 canais
  
```
   Canal                                                              | GPIO                                                               | Tipo
  --------------------------------------------------------------------|--------------------------------------------------------------------|--------------------------------------------------------------------
   CH0                                                                | 34                                                                 | Input-only, ADC1
   CH1                                                                | 35                                                                 | Input-only, ADC1
   CH2                                                                | 36 (VP)                                                            | Input-only, ADC1
   CH3                                                                | 39 (VN)                                                            | Input-only, ADC1
   CH4                                                                | 32                                                                 | ADC1
   CH5                                                                | 33                                                                 | ADC1

  │ Todos ADC1 porque ADC2 não funciona com Bluetooth ativo!

  ### 🔘 3 Telas (botão BOOT alterna)

  1. PIX → QR Code fixo (padrão)
  2. Monitor → Osciloscópio em tempo real, 6 canais coloridos
  3. Mapeamento → Lista de padrões salvos

  ### 💻 Comandos Serial (115200 baud)

  • SCAN → Varredura 2 segundos — aperte um botão no Kenwood durante!
  • SAVE VOL_UP → Grava o padrão do sinal com um nome
  • READ → Leitura instantânea de todos os canais
  • LIST → Mostra padrões mapeados
  • HELP → Lista todos os comandos

  ### ⚡ IMPORTANTE — Circuito de Proteção

  Veja o guia completo no artifact com diagrama de fiação. Resumo:

    Kenwood ──[10kΩ]──┬──[10kΩ]── GND
                      ├── Zener 3.3V ── GND
                      └── GPIO ESP32

  │ 🔜 Depois de mapear os sinais, me peça para criar a Fase 2: Controle Remoto — o ESP32 vai reproduzir os pulsos para controlar o Kenwood via Bluetooth!

```
