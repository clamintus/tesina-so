# Tesina di Sistemi - Bacheca Elettronica
-------------------
- Pure C, nessuna libreria esterna
- POSIX-compliant
- Protocollo binario efficiente
- Concorrente
- Real-Time
	- Notifiche TCP OOB e SIGURG
	- I/O multiplexing nel client
- Split-locked con 2 mutex/semafori
- Database post robusto
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
Sincronizzazione con POSIX Mutex invece che semafori System-V:
```shell
make POSIX_MUTEX=1
```


## Utilizzo
Server: 
```shell
./server
```
- File `serverconf`:
```
AllowGuests=<0/1>
Port=<port>
Title=<title> (opzionale)
```
- File `users`:
```
<user1> \x1f <pw1> \x1f <0/1> (is_admin1)
<user2> \x1f <pw2> \x1f <0/1> (is_admin2)
...
```
Client: 
```shell
./client <indirizzo> <porta>
```