# ssv6xxx: driver Linux para os Wi-Fi SSV6051 e SSV6256 (SDIO)

Driver para o mac80211, escrito do zero, para os chips da iComm (South
Silicon Valley) usados em TV boxes e placas embarcadas:

- **SSV6051**, vendido também como **SSV6030**: 802.11b/g em 2,4 GHz;
- **SSV6256**: 802.11a/b/g em 2,4 e 5 GHz.

Os dois rodam **só em legacy**, até 54 Mbit/s e canais de 20 MHz, de
propósito: sem HT (802.11n), sem HT40 e sem agregação. Nestes chips o
HT fica pior, não melhor. No SSV6256, na mesma caixa e no mesmo ponto
de acesso, a descida caiu de 23 Mbit/s em legacy para 5 Mbit/s com HT,
com o TCP retransmitindo sem parar: o chip perde quadros recebidos em
HT. O driver do fabricante chega ao mesmo lugar: ele também se associa
em legacy.

Os dois respondem à **mesma identidade SDIO** (0x3030:0x3030), então um
driver só atende aos dois: na inicialização, cada chip é perguntado se
reconhece o cartão, e o primeiro que se identifica fica com ele. Isso
evita o conflito que havia com dois módulos disputando o mesmo aparelho.

- **modos:** cliente (WPA2-PSK, WPA2-Enterprise/PEAP) e hotspot (AP com
  `hostapd`), um de cada vez;
- **plataformas:** 32 e 64 bits, independente de endianness;
- **configuração:** device tree, sem arquivo `.cfg` nem parâmetros;
- **estilo:** passa no `checkpatch.pl --strict`;
- **testado em:** kernel 6.18 (Armbian) em RK322x e S905X, e kernel 7.2
  (Armbian) em S805 (MXQ, `meson-mx-sdio`).

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
tem ainda `rc.c` (controle de taxa), e o SSV6256 tem `phy.c`.

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

Setembro de 2026, TCP, poucos segundos em cada sentido:

| chip, driver, ponto de acesso | descida | subida |
|---|---|---|
| SSV6256, este driver (legacy), 5 GHz, −47 dBm | 23,4 Mbit/s | 20,1 |
| SSV6256, este driver com HT40 (versão anterior) | 5,0 Mbit/s | 21,8 |
| SSV6256, driver do fabricante (cdhigh), mesmo AP | 20,3 Mbit/s | 15,7 |
| SSV6051, este driver (legacy), 2,4 GHz, −37 dBm | 5 Mbit/s | 6 |

O SSV6051 foi medido em outro lugar e com outro método (`nc` contra um
roteador DD-WRT, com um canal de 2,4 GHz disputado), então não se
compara às linhas do SSV6256.

No SSV6256, a descida depende também de o chip entregar os quadros
recebidos **em lote**, vários numa leitura só do SDIO. Sem o lote, mesmo
em legacy, ela cai para 5 Mbit/s. O lote vem sempre ligado; quando o
chip não aceita o formato, o driver volta a ler um quadro por vez.

## Limitações conhecidas

- Cliente e hotspot não funcionam ao mesmo tempo: o chip só aceita um
  endereço MAC.
- Sem power save 802.11.
- Só legacy: até 54 Mbit/s, 20 MHz, sem 802.11n (veja acima o porquê).
- SSV6051: confirmação real de envio só para gerência e EAPOL.
- No S805 o controlador SDIO do mainline (`meson-mx-sdio`) não tem
  interrupção SDIO: o kernel consulta o chip por polling, o que limita a
  vazão. O firmware é gravado em pedaços que cabem num comando só do
  host, porque esse controlador aceita no máximo 256 blocos por comando.
- Partes do código e as tabelas vêm do driver do fabricante, cujos
  cabeçalhos citam GPL versão 3 ou posterior; para o kernel oficial isso
  precisaria ser esclarecido com a iComm.
