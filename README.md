# ssv6xxx: driver Linux para os Wi-Fi SSV6051 e SSV6256 (SDIO)

Driver para o mac80211, escrito do zero, para os chips da iComm (South
Silicon Valley) usados em TV boxes e placas embarcadas:

- **SSV6051**, vendido também como **SSV6030**: 802.11b/g/n em 2,4 GHz,
  HT20 com short GI (até MCS7), agregação no envio e na recepção;
- **SSV6256**: acrescenta 5 GHz e HT40; recebe agregados (o MAC responde
  aos Block Ack sozinho) e não agrega no envio.

Os dois respondem à **mesma identidade SDIO** (0x3030:0x3030), então um
driver só atende aos dois: na inicialização, cada chip é perguntado se
reconhece o cartão, e o primeiro que se identifica fica com ele. Isso
evita o conflito que havia com dois módulos disputando o mesmo aparelho.

- **modos:** cliente (WPA2-PSK, WPA2-Enterprise/PEAP) e hotspot (AP com
  `hostapd`), um de cada vez;
- **plataformas:** 32 e 64 bits, independente de endianness;
- **configuração:** device tree, sem arquivo `.cfg` nem parâmetros;
- **estilo:** passa no `checkpatch.pl --strict`;
- **testado em:** kernel 6.18 (Armbian) em RK322x e S905X.

## Arquivos

| caminho | conteúdo |
|---|---|
| `main.c`, `ssv6xxx.h` | registro no barramento SDIO e escolha do chip |
| `ssv6051/` | o chip de 2,4 GHz (SSV6051/6030) |
| `ssv6256/` | o chip de banda dupla (SSV6256) |
| `Documentation/.../ssv,ssv6xxx.yaml` | binding de device tree |

Cada pasta de chip tem a mesma divisão: `sdio.c` (barramento e
firmware), `hw.c` (inicialização, canal, calibração), `mac.c`
(interface com o mac80211), `tx.c`/`rx.c`, `ap.c` (hotspot); o SSV6051
tem ainda `rc.c` (controle de taxa) e `ampdu.c` (agregação no envio), e
o SSV6256 tem `phy.c`.

## Compilar

```sh
sudo apt install build-essential linux-headers-$(uname -r)
make
sudo make install     # módulo em updates/, firmware em /lib/firmware/ssv/
```

O firmware de cada chip vai junto: `ssv6051-sw.bin` e `ssv6x5x-sw.bin`.

Remova antes os drivers antigos, que têm o mesmo nome de módulo do
fabricante e disputam o mesmo aparelho:

```sh
sudo find /lib/modules/$(uname -r) -name 'ssv6051*.ko*' -o -name 'ssv6256*.ko*' -delete
```

## Desempenho medido

Medições de setembro de 2026, com `iperf3` de dez segundos em cada
sentido. Elas dizem mais sobre o ambiente que sobre o driver: o 2,4 GHz
do lab tinha 25 redes no canal, e em casa o canal 1 ficou 70% ocupado a
noite toda.

| chip, ponto de acesso | subida | descida |
|---|---|---|
| SSV6051, 2,4 GHz canal 1, −36 dBm | 13 a 15 Mbit/s | 9 a 11 |
| SSV6256, 5 GHz canal 149 HT40, −20 dBm | 17 a 19 Mbit/s | 13 a 14 |

Para comparação, no mesmo rádio de 2,4 GHz e no mesmo horário, um
celular Wi-Fi 6 de duas antenas fez 12 Mbit/s de descida: ali o teto era
do ar, não do driver.

O que rendeu no SSV6051, e está aqui: o laço de envio dorme até o chip
avisar que há espaço, em vez de perguntar a cada volta (as consultas
caíram de ~430 para ~10 a cada 256 voltas), e o bit de QoS do chip passa
a ser ligado depois da associação, coisa que o driver do fabricante
também não faz.

O que **não** está aqui, por não ser estável: a leitura dos quadros
recebidos em lote no SSV6256. Ela dobra a descida (13,6 → 27 Mbit/s),
mas só inicia corretamente em cerca de um terço das cargas do módulo, e
às vezes trava o aparelho. Fica no ramo `lote-recepcao`, com as
medições e as hipóteses já descartadas registradas nos commits.

## Limitações conhecidas

- Cliente e hotspot não funcionam ao mesmo tempo: o chip só aceita um
  endereço MAC.
- Sem power save 802.11.
- SSV6051: sem HT40 (o rádio é de 20 MHz); confirmação real de envio só
  para gerência, EAPOL e agregados.
- SSV6256: não agrega no envio — o chip transmite o agregado, o outro
  lado responde com Block Ack e o chip ignora a resposta; o driver do
  fabricante também não agrega.
- Partes do código e as tabelas vêm do driver do fabricante, cujos
  cabeçalhos citam GPL versão 3 ou posterior; para o kernel oficial isso
  precisaria ser esclarecido com a iComm.
