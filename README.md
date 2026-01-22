# Tesina di Sistemi - Bacheca Elettronica

Un sistema di messaggistica client-server ad alte prestazioni per sistemi POSIX robusto, efficiente e portabile.
Sviluppato interamente in C puro e dipendente solo da libc.

## Caratteristiche
- Pure C, nessuna libreria esterna
- POSIX-compliant
- Protocollo binario ad-hoc ottimizzato ed efficiente
- Server concorrente multithreaded (thread-per-client)
- Real-Time
	- Notifiche asincrone TCP Out-Of-Band e segnali SIGURG
	- I/O multiplexing nel client, massima reattività
- Split-locked con 2 mutex/semafori
- Persistenza robusta
	- Database testuale "Create/Read/Delete" per i post
	- Atomic Save (salvataggio atomico tramite file temporaneo)
	- Sanitizzazione input
- Configurabile a 2 o 3 livelli di privilegi
	- admin: legge, scrive e cancella TUTTI i post
	- utente: legge, scrive e cancella i suoi post
	- (anonimo: legge)
- [TUI Responsive a 2 layout](#tui)
- [Portabile e Cross-Platform (testato su più architetture/sistemi)](#piattaforme)

Per la documentazione del protocollo vedere [`protocol.h`](protocol.h).

## TUI
<video src="https://github.com/user-attachments/assets/e8885461-a395-40ab-aba0-1799dc39450b"  controls="controls" muted="muted" autoplay="autoplay" loop="loop" style="max-width: 100%;"></video>

Il layer di presentazione del client è strutturato attraverso una TUI (Text User Interface) sviluppata interamente tramite sequenze di escape ANSI e attributi del terminale (_ioctl_, _termios_).
La TUI è adattiva e "responsive" perché possiede due layout che si attivano automaticamente in base alla grandezza del terminale:
- **Layout Standard**: Gli elementi grafici sono visualizzati in formato esteso. Quando c'è abbastanza spazio è automaticamente attivato.
- **Layout Mobile**: Gli elementi sono più compatti, ogni cella è sfruttata; molte label sono sostituite da [emoticon Unicode](https://en.wikipedia.org/wiki/Emoticons_(Unicode_block)) per compattezza, motivo per cui questo layout richiede il loro supporto da parte del terminale per essere visualizzato correttamente.

La corretta visualizzazione della UI è garantita su schermate che variano da enormi fino a molto piccole (~10 caratteri di altezza/larghezza).
Anche se la UI si dovesse corrompere temporaneamente su finestre ancora più piccole, la logica del programma è illesa grazie alla separazione netta tra il layer di presentazione e quello logico, che permette al client di continuare a funzionare normalmente.

## Piattaforme
Per validare l'effettiva portabilità e correttezza del codice platform-agnostic sia client che server sono stati compilati e testati su diverse piattaforme:

| Architettura | Sistema | Ambiente | Impl. Libc | Client | Server | Interoperabilità | Note |
|:-------------|:--------|:---------|:-----------|:------:|:------:|:-----:|:-----|
| x86_64 | GNU/Linux | standard | glibc | ✅ | ✅ | ✅ | Implementazione base |
| ARMv8  | Android | Termux | Bionic | ✅ | ✅ | ✅ | Test di ambiente mobile |
| MIPS   | GNU/Linux (QEMU, host x86_64) | Emulaz. userspace | glibc | ✅ | ✅ | ✅ | Test Big-Endian del protocollo |
| ARMv8  | Horizon (Nintendo Switch) | Homebrew | newlib | ✅ | ✅ | ✅ | Test embedded. Esegue nativamente sul sistema proprietario **Horizon** via devkitPro/Homebrew. Vedi [Nintendo Switch](#nintendo-switch) |

- La colonna **Interoperabilità** certifica il successo dei test in scenario eterogeneo (es. server MIPS connesso a client x86_64), confermando l'indipendenza del protocollo binario dall'endianness e dall'allineamento della memoria della macchina ospite.
- **Toolchain di compilazione usate**:
	- x86_64: `gcc` standard
	- Android: `clang` (Android NDK)
	- MIPS: `mips64-linux-gnu-gcc`
	- Nintendo Switch: `aarch64-none-elf-gcc` dalla toolchain _devkitPro_
## Nintendo Switch

Il port su **Nintendo Switch**, sviluppato nella branch `switch-port` e mantenuto allineato al `master` nel tempo, è stato realizzato con l'obiettivo di verificare se la portabilità e l'efficienza del sistema fossero idonee anche a un contesto embedded caratterizzato da vincoli di esecuzione molto più stringenti rispetto a un desktop standard.
- La Switch è una console da gioco prodotta da Nintendo nel 2017. È a tutti gli effetti un dispositivo embedded ad alte prestazioni (4 GB di RAM condivisa, SoC NVIDIA Tegra X1 a 4 core con frequenza di clock ottimizzata per il consumo energetico) che mette a disposizione **un ambiente di esecuzione limitato** dettato dalle scelte di design architetturale del produttore (gestione aggressiva delle risorse in favore dell'autonomia della batteria e della prevedibilità del sistema).
- Esegue un sistema proprietario, Horizon, che:
    - ha un'architettura a microkernel;
    - non è nativamente POSIX;
    - supporta il multithreading preemptive, ma impone vincoli stretti sull'utilizzo delle risorse dei thread secondari (stack size e assenza di swap);
    - impone un modello di esecuzione ciclico non bloccante (Game Loop) che richiede l'invocazione periodica della funzione `appletMainLoop` (tipicamente ogni tempo di frame) che permette il rilascio del controllo al framework di sistema (gestione apertura menu HOME, standby della console, ...);
- La toolchain _devkitPro_ permette di compilare codice C eseguibile nativamente sulla console, fornisce parzialmente compatibilità POSIX tramite un'implementazione di `newlib` e offre una libreria userland, `libnx`, per interfacciarsi con Horizon.
- Rimangono assenti dal layer di compatibilità diverse caratteristiche dei sistemi POSIX come i segnali, sfruttati in modo esteso dall'implementazione base, chiaramente non implementabili perché il kernel non li usa.

Il codice è stato portato su Switch mantenendo la sua architettura pressoché intatta: le uniche modifiche apportate riguardano:
- **la gestione delle idiosincrasie del sistema**: ad esempio interazione hardware/software tramite `libnx` e gestione input con tastiera software di sistema (_Swkbd_);
- **gestione rigorosa delle risorse**: rispetto dei vincoli sulla stack, dei thread secondari, sulle connessioni TCP (il servizio BSD di sistema permette massimo 16 connessioni simultanee) e ottimizzazione ad-hoc dei buffer TCP;
- **il passaggio a un approccio di polling non bloccante** su input e rete nel loop principale per rientrare nel vincolo del Game Loop;
- **gestione sincrona degli eventi** (TCP Urgent data ed errori di rete), data l'assenza dei segnali UNIX;
- **l'astrazione dell'interfaccia utente**: adattamento delle label dei tasti in modo contestuale al target di compilazione.

_Nota: L'intero porting contiene tutta la logica di `master` ed è stato sviluppato attraverso l'aggiunta di blocchi di compilazione condizionale `#ifdef` sulla base del codice principale: questo rende l'intera base di codice del porting retrocompatibile con gli altri target. Di conseguenza si può compilare il sistema per gli altri target anche da questo ramo attraverso il Makefile alternativo `Makefile.linux`._

## Compilazione
### Standard (Linux/Android)
Il Makefile rileva automaticamente l'ambiente Linux oppure Android e compila per il giusto target di conseguenza.

```shell
make
```
Per usare l'implementazione della sincronizzazione con POSIX Mutex invece che semafori System-V:
```shell
make POSIX_MUTEX=1
```
### Porting (Nintendo Switch)

Prerequisiti: [ambiente devkitPro](https://switchbrew.org/wiki/Setting_up_Development_Environment) con il metapackage `switch-dev` installato
```bash
git checkout switch-port
make
```

### Cross-Arch (Linux su MIPS o altre architetture)
Il Makefile è flessibile e supporta l'override del compilatore usato nello script.
```bash
CC=mips64-linux-gnu-gcc make
# per provare il programma da un'architettura diversa basta usare QEMU userland:
qemu-mips64 server	
```


## Utilizzo
### Server
```shell
./server
```
Il server ha bisogno di essere configurato: vedi [Configurazione](#configurazione).


### Client
```shell
./client <indirizzo> <porta>
```

## Configurazione
Il server ha bisogno di un file di configurazione `serverconf` nella stessa directory dell'eseguibile, strutturato come segue:
- File `serverconf`: formato `key=value`, chiavi valide:
	- `AllowGuests`: abilita il terzo livello di privilegi (**int**, 0/1, default 0)
	- `Port` (_opzionale_): indica la porta di ascolto da utilizzare (**int**, 0~65535, default 3010)
	- `Title` (_opzionale_): indica il titolo della bacheca che i client mostreranno nella schermata introduttiva e nell'header nella TUI (**string**, max 250 caratteri)
    - `Timeout` (_opzionale_): indica il timeout di disconnessione delle sessioni dei client in secondi (**int**, 1\~_INT\_MAX_, default 900 (15 min))

Esempio:
```ini
AllowGuests=0
Port=3000
Title=Progetto Sistemi
Timeout=900
```
Se il server è configurato per non accettare sessioni anonime, necessita obbligatoriamente di aver configurato almeno un utente.
Gli utenti sono letti da un database testuale presente nella stessa directory dell'eseguibile:
- File `users`: record testuali (uno per riga) con campi separati dal carattere ASCII 0x1F (Unit Separator). Ogni utente ha i seguenti campi:
	- **username**
	- **password**
	- **is_admin** (0 oppure 1)
```
<user1> \x1f <pw1> \x1f <is_admin1>
<user2> \x1f <pw2> \x1f <is_admin2>
...
```
**Per aggiungere un utente è possibile e consigliato utilizzare lo script `useradd.sh` fornito**:
```bash
./useradd.sh <username> <password> <is_admin>
```
-------------
## Benchmarking & QA
Nella repository è presente anche una suite di stress test scritta in Python che ho utilizzato per verificare la robustezza e valutare l'efficienza del sistema (e arrivare a circa il 100% di code coverage):
- `stress-connect.py`: Flood di connessioni e disconnessioni rapide (verifica la gestione della memoria e dei thread)
```bash
stress-tests/stress-connect.py <indirizzo> <porta> <n_connessioni>
```
- `stress-connect2.py`: Flood di connessioni simultanee (riempie la tabella dei file descriptors e mette sotto carico la RAM del server)
```bash
stress-tests/stress-connect2.py <indirizzo> <porta> <n_connessioni_simultanee>
```
- `stress-post.py`: Posta 1000 post piccoli in rapida successione (testa la concorrenza se eseguito parallelamente ad altre sue istanze)
```bash
stress-tests/stress-post.py <indirizzo> <porta> <ID test>
```
- `stress-postsize.py`: Posta 50 post di grandezza massima (60KB) in rapida successione (testa gli edge-case dei buffer e misura il collo di bottiglia del sistema (che è I/O-bound)) 
```bash
stress-tests/stress-postsize.py <indirizzo> <porta>
```
Nel file `stress-postsize-out.txt` è mostrato il risultato dell'esecuzione di due batch da 4 invocazioni di `stress-postsize` effettuate in successione e il calcolo del throughput massimo del sistema effettuato sull'ultima invocazione. **Da questo risultato ho appurato la natura I/O bound del sistema, il cui throughput massimo reale è molto vicino al limite hardware (velocità del disco).**

