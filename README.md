# tic-tac-toe
1. How to Compile and Run
The system requires a Linux environment and the GCC compiler.

Compilation
A Makefile is provided to automate the build process. To compile both the server and the client, navigate to the project directory and run: make 

This command compiles the source files using gcc with the mandatory -pthread flag to support multithreading.

Running the System
The server must be started before any clients can connect.


Start the Server: ./server 


Start the Clients: Open at least three separate terminal windows and run the client executable in each: ./client 

2. Example Commands

Compile: make 


Run Server: ./server 


Run Client (Localhost): ./client 127.0.0.1 

Clean Build Files: make clean (If implemented in your Makefile)


Manual Shutdown: Press Ctrl+C in the server terminal to trigger the SIGINT handler, which saves scores to scores.txt and cleans up shared memory.

3. Game Rules Summary
This is a text-based, turn-based board game enforced entirely by the server.


Player Count: Exactly 3 players are required to start a game session.


Grid: The game is played on a 4x4 grid with 16 possible move positions (indexed 0-15).

Symbols: Players are assigned symbols 'X', 'O', or 'A' upon connection.


Turn Management: Moves are managed via a Round Robin scheduler thread.


Winning Condition: A player wins by successfully placing 3 of their symbols in a horizontal, vertical, or diagonal row.


Draw Condition: If all 16 positions are filled and no player has achieved a winning line, the game ends in a draw.


Persistence: Wins are tracked across games and saved in scores.txt.


Successive Play: The server resets the board automatically after a game ends, allowing for multiple games without a restart.

4. Mode Supported
The system is implemented in Multi-machine mode.


Protocol: Communication between the client child processes and the remote clients is handled via TCP sockets (IPv4).


Internal IPC: While network sockets handle external communication, the server uses POSIX Shared Memory and Process-Shared Mutexes for internal coordination between the parent threads and child processes.
