# Elevator Scheduling System

**Author:** Halmuhammet Muhamedorazov  
**Date:** April 28, 2024  
**Version:** 1.0  
**C++ Version:** g++ (GCC) 4.8.5 20150623 (Red Hat 4.8.5-16)

---

## 📝 Description

This project is a multithreaded elevator scheduling system implemented in C++. It simulates the behavior of a building’s elevator system using concurrent threads to handle:

- **Reader Thread**: Continuously polls for elevator requests via HTTP GET.
- **Scheduler Thread**: Processes the queue of waiting people and assigns the most suitable elevator based on direction, range, and capacity.
- **Elevator Assigner**: Communicates elevator assignments using HTTP PUT requests.

The simulation fetches real-time data from local HTTP endpoints and schedules elevators efficiently using synchronized threads, queues, and conditional variables.

---

## ⚙️ Features

- Multithreaded design using C++11 threads and `mutex`/`condition_variable`.
- Elevator assignment logic based on:
  - Current elevator position
  - Travel direction
  - Remaining capacity
  - Start and end floor
- Integration with RESTful API using `libcurl`.
- Live polling of elevator and simulation status via HTTP GET/PUT.
- Console output for debugging and progress tracking.

---

## 🧩 Dependencies

- C++11 or later
- [libcurl](https://curl.se/libcurl/) (for HTTP communication)

---

## 🛠️ Compilation

Use the following command to compile:

```bash
g++ -std=c++11 scheduler_os.cpp -o scheduler_os -lcurl -pthread
```

Ensure you have installed `libcurl` and linked it with `-lcurl`.

---

## 🚀 Usage

Once compiled, run the binary:

```bash
./scheduler_os
```

Make sure the local server hosting the simulation is running and listening on port `5432`. The system will automatically:

1. Continuously check simulation status via:
   ```
   GET http://localhost:5432/Simulation/check
   ```

2. Request next person needing elevator via:
   ```
   GET http://localhost:5432/NextInput
   ```

3. Send elevator assignment:
   ```
   PUT http://localhost:5432/AssignElevator/{elevator_id}/{person_id}
   ```

---

## 📂 File Structure

- `scheduler_os.cpp` – Main program file containing logic for all threads and simulation interaction.

---

## 🧵 Thread Overview

- **Reader Thread**  
  - Polls for simulation status and new elevator requests.
  - Adds requests to the shared `people` queue.

- **Scheduler Thread**  
  - Waits for new people in the queue.
  - Matches them with an appropriate elevator based on location, direction, and availability.

- **Elevator Assigner**  
  - Sends a PUT request to assign the selected elevator to the waiting person.

---

## 🔐 Synchronization

- Shared resources (`people`, `elevators`, etc.) are protected using `std::mutex`.
- Condition variables (`cv_scheduler`, `cv_addToElevator`) ensure threads wait efficiently and wake only when necessary.

---

## 🧪 Example Output

Console output might look like:

```
Reader : personID: 102 startFloor: 3 endFloor: 8
Assigning elevator for : PersonID: 102 startFloor: 3 endFloor: 8
Assigned Elevator E3 to PersonID: 102
```

---

## 🛑 Notes

- This program is part of a larger elevator simulation and assumes proper API response formats.
- Make sure your server (e.g., a Flask or Node.js mock API) correctly handles `/Simulation/check`, `/NextInput`, and `/ElevatorStatus/{id}`.

---

## 📜 License

This project is for educational purposes and is licensed under the MIT License.

---
