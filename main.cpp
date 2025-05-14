/*=============================================================================*
 * Title        : os.cpp
 * Description  : This is a multithreading program. It utilizes reader and worker function using multiple threads concurrently.
                  The worker function classifies and writes the numbers into their respective files.
 * Author       : Halmuhammet Muhamedorazov
 * Date         : 03/22/2024
 * Version      : 1.0
 * Usage        : Compile and run this program using the GNUC++ compiler
 * Notes        : To compile the program, don't forget to include -pthread to link the C++ threading libraries.
 * C++ Version  : g++ (GCC) 4.8.5 20150623 (Red Hat 4.8.5-16)
 * =============================================================================*/

#include <iostream>
#include <curl/curl.h>
#include <queue>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <mutex>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <algorithm>

using namespace std;

//declare a global queue that contains next person to handle, elevators, and assigned elevators
deque <deque <string>> people;
deque <deque <string>> elevators;
deque <string> assignedElevator;

mutex mtx;
condition_variable cv_scheduler; // condition variable for scheduler thread
condition_variable cv_addToElevator; // condition varable for the reader
bool endOfInput = false;  // initialize the end of input to false
bool everyoneAssignedElevator = false;

// Callback function to handle the response
size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

bool sortByRemainingCapacity(deque <string>& a, deque <string>& b) {
    int aInt = stoi(a[4]);
    int bInt = stoi(b[4]);
    return aInt > bInt; // Sort in descending order of remaining capacity
}

void init_put(string url) {
    // Initialize libcurl
    curl_global_init(CURL_GLOBAL_ALL);
    // Create a curl handle
    CURL* curl = curl_easy_init();
    if (curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
//        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
        // Perform the PUT request
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        }
        // Cleanup the curl handle
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
}

//init_get only needs a url to get the value in /initialize and /modify
//it return an integer.
string init_get(string url) {
    // Initialize libcurl
    curl_global_init(CURL_GLOBAL_ALL);
    std:: string returnInt;
    std::string buffer;
    // Create a new curl handle for the GET request
    // Create a curl handle
    CURL* curl = curl_easy_init();
    if (curl) {
        // Set the URL for the GET request
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        // Perform the GET request
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        }
        // Cleanup the curl handle
        curl_easy_cleanup(curl);
    }
    // Cleanup libcurl
    curl_global_cleanup();
    // return the value in buffer as int
    return buffer;
}

void reader(){



    string simulationStatus = init_get("http://localhost:5432/Simulation/check");
    cout<<"inside scheduler: "<<simulationStatus <<endl;
    while(simulationStatus == "Simulation is running.") {

        string nextInput = init_get("http://localhost:5432/NextInput");
        cout<<"next input "<< nextInput<<endl;

        auto startTime = chrono::steady_clock::now();

        while(nextInput == "NONE"){
            auto elapsedTime = chrono::steady_clock::now() - startTime;
            if(elapsedTime >= chrono::seconds(20)){

                simulationStatus = init_get("http://localhost:5432/Simulation/check");

                if(simulationStatus != "Simulation is running."){
                    break;
                }
            }
            chrono::milliseconds duration(500); // 0.5 seconds
            this_thread::sleep_for(duration);
            cout<<"sleeping"<<endl;
            nextInput = init_get("http://localhost:5432/NextInput");
        }
        if(simulationStatus != "Simulation is running."){
            break;
        }

        // Create variables to store personID, startFloor, and endFloor
        string personID, startFloor, endFloor;

        // Create a stringstream to parse the inputString
        istringstream iss(nextInput);

        // Use getline to extract the values separated by '|'
        getline(iss, personID, '|'); // Extract personID
        iss >> startFloor; // Extract startFloor
        iss.ignore(); // Ignore the delimiter '|' after startFloor
        getline(iss, endFloor); // Extract endFloor

        // Check if extraction was successful
        if (iss.fail()) {
            // Handle extraction failure
            cout << "Extraction failed." << endl;
        }
        cout<<"Reader : "<<"personID: "<< personID<< " startFloor: "<<startFloor<<" endFloor"<<endFloor<<endl;

        deque <string> person;


        person.push_back(personID);
        person.push_back(startFloor);
        person.push_back(endFloor);

        cout<<"Person:\n";

        for(int i = 0; i < person.size(); i++){
            cout<< person[i]<<"\t";

        }
        // lock the shared queue people to make sure only one thread at a time can access it
        lock_guard<std::mutex> lock(mtx);
        people.push_back(person);
        // when a person is pushed into shared people queue, notify the scheduler threads to wake up
        cv_scheduler.notify_all();

        cout<<"People:\n";
        for(int i = 0; i < people.size(); i++){
            for(int j = 0; j < people[0].size(); j++){
                cout<< people[i][j]<<"\t";
            }
            cout<<endl;
        }

        simulationStatus = init_get("http://localhost:5432/Simulation/check");
        cout<<"READER: Simulation status: "<<simulationStatus<<endl;

    }
    lock_guard<std::mutex> lock(mtx);
    // use a variable to indicate if the reader reached the end of file
    endOfInput = true;
    // then notify the worker threads
    cv_scheduler.notify_all();
}

void schedule_elevator(){
    while(true){
        // lock the shared resources to make sure only one thread at a time accesses them
        unique_lock<mutex> lock(mtx);
        // wait if the shared buffer is empty and the reader has not reached the end of file yet
        while(people.empty() && endOfInput == false) {
            cv_scheduler.wait(lock);
        }

        if(endOfInput == true && people.empty()){
            break;
        }
        deque <string> personWaitingElevator = people.front();

        string personID = personWaitingElevator[0];
        int startFloor = stoi(personWaitingElevator[1]);
        int endFloor = stoi(personWaitingElevator[2]);

        string needUpOrDown;
        if (endFloor - startFloor >= 0) {
            needUpOrDown = "U";
        } else {
            needUpOrDown = "D";
        }
        cout<<"after parsing next person"<<endl;

        string elevatorStatus;

        string closestElevator;
        size_t length = elevators.size();

        for(int i = 0; elevators.size(); i++){
            elevatorStatus = init_get("http://localhost:5432/ElevatorStatus/" + elevators[i][0]);

            string bayID, directionString, currentFloor, passengerCount, remainingCapacity;

            istringstream iss(elevatorStatus);

            // Use getline to extract the values separated by '|'
            if (getline(iss, bayID, '|') &&
                getline(iss, currentFloor, '|') &&
                getline(iss, directionString, '|') &&
                getline(iss, passengerCount, '|') &&
                getline(iss, remainingCapacity)) {
                // Parsing successful, do something with the values
            } else {
                // Parsing failed, handle the error
                cerr << "Error parsing elevator status." << endl;
            }

            elevators[i][3] = currentFloor;
            elevators[i][4] = remainingCapacity;
        }

        sort(elevators.begin(), elevators.end(), sortByRemainingCapacity);

        for (size_t i = 0; i < length; i++) {
            cout << "loop counter number: " << i << endl;
            if ((stoi(elevators[i][1]) <= startFloor) &&
                (stoi(elevators[i][2]) >= startFloor) &&
                (stoi(elevators[i][2]) >= endFloor) &&
                (stoi(elevators[i][1]) <= endFloor))
            {
                //request elevator status
                elevatorStatus = init_get("http://localhost:5432/ElevatorStatus/" + elevators[i][0]);
                cout << "inside the scheduling logic. Elevator Status: " << elevatorStatus << endl;
                string bayID, directionString;
                int currentFloor, passengerCount, remainingCapacity;

                istringstream iss(elevatorStatus);

                // Use getline to extract the values separated by '|'
                if (getline(iss, bayID, '|') &&
                    iss >> currentFloor &&
                    iss.ignore(1, '|') &&
                    getline(iss, directionString, '|') &&
                    iss >> passengerCount &&
                    iss.ignore(1, '|') &&
                    iss >> remainingCapacity) {
                    // Parsing successful, do something with the values
                } else {
                    // Parsing failed, handle the error
                    cerr << "Error parsing elevator status." << endl;
                }
                cout << "currentFloor: " << currentFloor << " DirectionString: " << directionString
                     << " passengerCount: " << passengerCount << " remainingCapacity: " << remainingCapacity << endl;
                if (remainingCapacity > 0) {
                    closestElevator = elevators[i][0];
//                    if (directionString == "S") {
//                        closestElevator = elevators[i][0];
//                    } else if (needUpOrDown == directionString && currentFloor < startFloor && needUpOrDown == "U") {
//                        closestElevator = elevators[i][0];
//                    }else if (needUpOrDown == directionString && currentFloor > startFloor && needUpOrDown == "D") {
//                        closestElevator = elevators[i][0];
//                    }
                }

            }
        }


        string nextPerson = personID + "/" + closestElevator;
        assignedElevator.push_back(nextPerson);

        cv_addToElevator.notify_all();

        cout<<"next person with elevator assigned: "<<nextPerson<<+"/"+closestElevator<<endl;

        people.pop_front();
    }
    lock_guard<std::mutex> lock(mtx);;
    // use a variable to indicate if the reader reached the end of file
    everyoneAssignedElevator = true;
    // then notify the worker threads
    cv_addToElevator.notify_all();

}



void add_person_to_elevator(){
    while(true){
        // lock the shared resources to make sure only one thread at a time accesses them
        unique_lock<mutex> lock(mtx);
        // wait if the shared buffer is empty and the reader has not reached the end of file yet
        while(assignedElevator.empty() && everyoneAssignedElevator == false) {
            cv_addToElevator.wait(lock);
        }

        if(everyoneAssignedElevator == true && assignedElevator.empty()){
            break;
        }

        string addToElevator = "http://localhost:5432/AddPersonToElevator/" + assignedElevator.front();
        init_put(addToElevator);
        assignedElevator.pop_front();
    }

}

int main(int argc, char* argv[]) {
    // Check if at least one command-line argument (besides the program name) is provided
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <input_building_file>" << endl;
        return 1; // Return error code 1 indicating incorrect usage
    }

    // Extract the input building file path from the command-line arguments
    string input_building = argv[1];
    // Open the file
    ifstream file(input_building);
    if (!file.is_open()) {
        cerr << "Error opening file." << endl;
        return 1;
    }

    string line;
    while (getline(file, line)) {
        istringstream iss(line);
        string token;

        // Vector for the current elevator
        std::deque<std::string> elevator;

        while (std::getline(iss, token, '\t')) {
            elevator.push_back(token);
        }

        // Push the current elevator vector to elevators
        elevators.push_back(elevator);
    }

    // Close the file
    file.close();

    for(int i = 0; i < elevators.size(); i++){
        for(int j = 0; j < elevators[0].size(); j++){
            cout << elevators[i][j]<<"\t";
        }
        cout<<endl;
    }

    init_put("http://localhost:5432/Simulation/start");
    thread read(reader);
    thread schedule(schedule_elevator);
    thread addToElevator(add_person_to_elevator);

    read.join();
    schedule.join();
    addToElevator.join();

    return 0;
}
