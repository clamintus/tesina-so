# Tesina di Sistemi - Bacheca Elettronica
-------------------
Un sistema di messaggistica per sistemi POSIX-like
- Pure C, nessuna libreria esterna
- POSIX-compliant
- Protocollo binario ad-hoc ottimizzato
- Concorrente
- Real-Time
	- Notifiche TCP Out-Of-Band e segnali SIGURG
	- I/O multiplexing nel client
- Split-locked con 2 mutex/semafori
- Database per persistenza post robusto
	- Atomic Save
	- Sanitizzazione input
- Configurabile a 2 o 3 livelli di privilegi
	- admin: legge, scrive e cancella TUTTI i post
	- utente: legge, scrive e cancella i suoi post
	- (anonimo: legge)
- TUI Responsive a 2 layout

## Compilazione
```shell
make
```
Per usare l'implementazione della sincronizzazione con POSIX Mutex invece che semafori System-V:
```shell
make POSIX_MUTEX=1
```


## Utilizzo
### Server
```shell
./server
```
Il server ha bisogno di un file di configurazione `serverconf` e (se necessario) di un database di utenti `users`, entrambi nella stessa directory dell'eseguibile.
- File `serverconf`: formato `key=value`, chiavi valide:
	- `AllowGuests`: abilita il terzo livello di privilegi (**int**, 0/1)
	- `Port`: indica la porta di ascolto da utilizzare (**int**, 0~65535)
	- `Title` (_opzionale_): indica il titolo della bacheca che i client mostreranno nella schermata introduttiva e nell'header nella TUI (**string**, max 250 caratteri)

Esempio:
```
AllowGuests=0
Port=3000
Title=Progetto Sistemi
```
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


### Client
```shell
./client <indirizzo> <porta>
```

-------------
## Benchmarking
Nella repository è presente anche una suite di stress test scritta al volo in Python che ho utilizzato per verificare la robustezza e, per pura curiosità, l'efficienza del sistema (e arrivare a circa il 100% di code coverage):
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