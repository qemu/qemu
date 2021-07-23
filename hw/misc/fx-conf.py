from pwn import *

interval = int(input("Set conf interval fx-thread: "))

with remote('localhost', 3333) as conn: 
    conn.send(p64(interval))
    