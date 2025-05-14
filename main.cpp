/*=============================================================================*
 * Title        : scheduler_os.cpp
 * Description  : This is an elevator scheduling program.
                  It utilizes reader, scheduler, and a elevator assigner function usings multiple threads concurrently.
 * Author       : Halmuhammet Muhamedorazov
 * Date         : 04/28/2024
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

//declare mutex locks and condition variables for thread synchronization
mutex mtx;
condition_variable cv_scheduler; // condition variable for scheduler thread
condition_variable cv_addToElevator; // condition variable for the reader
bool endOfInput = false;  // initialize the end of input to false
bool everyoneAssignedElevator = false; //initialize boolean for determining if everyone was assigned an elevator to false

// Callback function to handle the response
size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

//This function is used to sort the queue by elevator distance in ascending order
bool sortByElevatorDistance(const deque <string>& a, const deque <string>& b) {
    int aInt = stoi(a[8]);
    int bInt = stoi(b[8]);
    return aInt < bInt; // Sort in ascending order of remaining capacity
}

//This function is used to sort the queue by elevator distance in descending order
bool sortByRemainingCapacity(const deque <string>& a, const deque <string>& b) {
    int aInt = stoi(a[5]);
    int bInt = stoi(b[5]);
    return aInt > bInt;
}

//This function is used to send requests as PUT method
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

//This function is used to get response from a curl request by GET method
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

//This function reads next person who needs to use elevator and puts them into a
//global queue people.
void reader(){

    string simulationStatus = init_get("http://localhost:5432/Simulation/check");

    //While the simulation is running, keep pinging for then next person
    while(simulationStatus == "Simulation is running.") {
        string nextInput = init_get("http://localhost:5432/NextInput");

        //If the response is NONE, then sleep for 0.8 seconds and then request if person is available
        while(nextInput == "NONE"){

            //While sleeping, check if the simulation is complete. If completed, we can terminate the program.
            simulationStatus = init_get("http://localhost:5432/Simulation/check");
            if(simulationStatus == "Simulation is complete."){
                break;
            }

            //sleep for 0.8 seconds
            chrono::milliseconds duration(800);
            this_thread::sleep_for(duration);
            cout<<"\nsleeping"<<endl;
            nextInput = init_get("http://localhost:5432/NextInput");
        }
        if(simulationStatus == "Simulation is complete."){
            break;
        }


        // Create variables to store personID, startFloor, and endFloor
        string personID, startFloor, endFloor;

        // Create a stringstream to parse the inputString
        istringstream iss(nextInput);

        // Use getline to extract the values separated by '|'
        getline(iss, personID, '|'); // Extract personID
        getline(iss, startFloor, '|'); // Extract startFloor
        getline(iss, endFloor); // Extract endFloor

        if (iss.fail()) {
            // Handle extraction failure
            cout << "Extraction failed." << endl;
        }
        cout<<"\nReader : "<<"personID: "<< personID<< " startFloor: "<<startFloor<<" endFloor: "<<endFloor<<endl;

        //Declare a person queue to put them int a global queue
        deque <string> person;
        //Push the information regarding each person in person queue
        person.push_back(personID);
        person.push_back(startFloor);
        person.push_back(endFloor);

        cout<<"\nPerson:\n";

        for(int i = 0; i < person.size(); i++){
            cout<< person[i]<<"\t";

        }

        // lock the shared queue people to make sure only one thread at a time can access it
        lock_guard<std::mutex> lock(mtx);
        people.push_back(person);
        // when a person is pushed into shared people queue, notify the scheduler threads to wake up
        cv_scheduler.notify_all();

        cout<<"\nPeople:\n";
        for(int i = 0; i < people.size(); i++){
            for(int j = 0; j < people[0].size(); j++){
                cout<< people[i][j]<<"\t";
            }
            cout<<endl;
        }

        simulationStatus = init_get("http://localhost:5432/Simulation/check");
        cout<<"Simulation status at the end of while loop: "<<simulationStatus<<endl;

    }
    lock_guard<std::mutex> lock(mtx);
    // use a variable to indicate if the reader reached the end of input
    endOfInput = true;
    // then notify the scheduler thread
    cv_scheduler.notify_all();
}

//Scheduler takes the person in front of the global people queue.
//It then assigns to him/her elevator based on elevator distance from the requestor,
//which direction s/he needs to go, the remaning capacity of the elevators.
void schedule_elevator(){
    while(true){
        // lock the shared resources to make sure only one thread at a time accesses them
        unique_lock<mutex> lock(mtx);
        // wait if the shared buffer is empty and the reader has not reached the end of file yet
        while(people.empty() && endOfInput == false) {
            cv_scheduler.wait(lock);
        }

        //Break the loop if people queue is empty and the reader reached end of input.
        if(endOfInput == true && people.empty()){
            break;
        }

        //Declare a queue for people waiting for an elevator and initialise it with the front element of people queue
        deque <string> personWaitingElevator = people.front();
        //Extract the information regarding person ID, start floor, and end floor.
        string personID = personWaitingElevator[0];
        int startFloor = stoi(personWaitingElevator[1]);
        int endFloor = stoi(personWaitingElevator[2]);

        //Determine if the person needs to go up or down
        string needUpOrDown;
        if (endFloor - startFloor >= 0) {
            needUpOrDown = "U";
        } else {
            needUpOrDown = "D";
        }
        cout<<"\nAssigning elevator for : PersonID: "<<personID<<" startFloor: "<<startFloor<<" endFloor: "<<endFloor<<endl;

        //Declare variables for elevator status and closest elevator ID
        string elevatorStatus;
        string closestElevator;
        size_t length = elevators.size();

        //Select the candidate elevators that fall within the range of person travel
        deque <deque<string>> candidateElevators;
        for(size_t i = 0; i < length; i++){
            if ((stoi(elevators[i][1]) <= startFloor) &&
                (stoi(elevators[i][2]) >= startFloor) &&
                (stoi(elevators[i][2]) >= endFloor) &&
                (stoi(elevators[i][1]) <= endFloor)) {

                elevatorStatus = init_get("http://localhost:5432/ElevatorStatus/" + elevators[i][0]);

                //extract elevator status
                istringstream iss(elevatorStatus);
                string bayID, currentFloor, directionString, passengerCount, remainingCapacity;

                if (getline(iss, bayID, '|') &&
                    getline(iss, currentFloor, '|') &&
                    getline(iss, directionString, '|') &&
                    getline(iss, passengerCount, '|') &&
                    getline(iss, remainingCapacity)) {
                    // Parsing successful, do something with the values
                    cout << "\nbayID: " << bayID << endl;
                    cout << "currentFloor: " << currentFloor << endl;
                    cout << "directionString: " << directionString << endl;
                    cout << "passengerCount: " << passengerCount << endl;
                    cout << "remainingCapacity: " << remainingCapacity << endl;
                } else {
                    // Parsing failed, handle the error
                    cerr << "Error parsing elevator status." << endl;
                }

                //Update elevator information
                elevators[i][3] = currentFloor;
                elevators[i][5] = remainingCapacity;
                elevators[i][6] = directionString;
                elevators[i][7] = to_string(startFloor);

                int elevatorDistance = abs(startFloor - stoi(currentFloor));
                elevators[i][8] = to_string(elevatorDistance);
                elevators[i][9] = passengerCount;

                //Push the elevator into candidate elevators list
                candidateElevators.push_back(elevators[i]);
                cout<<"\nCONTENTS OF ELEVATORS";
                cout<<"\nElevatorID: "<< elevators[i][0]<<"\tCurrentFloor elevators[i][3]: "<<elevators[i][3]<<endl;
                cout<<"\nElevatorID: "<< elevators[i][0]<<"\tTotalCapacity elevators[i][4]: "<<elevators[i][4]<<endl;
                cout<<"\nElevatorID: "<< elevators[i][0]<<"\tremainingCapacity elevators[i][5]: "<<elevators[i][5]<<endl;
                cout<<"\nElevatorID: "<< elevators[i][0]<<"\tdirectionString elevators[i][6]: "<<elevators[i][6]<<endl;
                cout<<"\nElevatorID: "<< elevators[i][0]<<"\tstartFloor elevators[i][7]: "<<elevators[i][7]<<endl;
                cout<<"\nElevatorID: "<< elevators[i][0]<<"\televatorDistance elevators[i][8]: "<<elevators[i][8]<<endl;
                cout<<"\nElevatorID: "<< elevators[i][0]<<"\tpassengerCount elevators[i][9]: "<<elevators[i][9]<<endl;


            }
        }
        //elevators queue: 0 ID | 1 Low | 2 High | 3 CurrentFloor | 4 Capacity |
        //                 5 remainingCapacity | 6 directionString | 7 startFloor | 8 elevatorDistance | 9 passengerCount

        //Check if there is a stationary elevator that does not have person in it
        size_t candidateElevatorsLength = candidateElevators.size();
        deque <deque<string>> stationaryElevatorsNoPassenger;
        for(size_t i = 0; i < candidateElevatorsLength; i++){

            string directionString = candidateElevators[i][6];
            int passengerCount = stoi(elevators[i][9]);
            //Put stationary elevators with no passenger into a new queue.
            if(directionString == "S" && passengerCount == 0){
                stationaryElevatorsNoPassenger.push_back(candidateElevators[i]);
            }
        }

        //Sort the stationary elevators with no passenger based on their distance from person requesting the elevator.
        sort(stationaryElevatorsNoPassenger.begin(), stationaryElevatorsNoPassenger.end(), sortByElevatorDistance);

        cout<<"\nDistance of Stationary elevators after sorting: "<<endl;
        for(int i = 0; i < stationaryElevatorsNoPassenger.size(); i++){
            cout<<"ID :"<<stationaryElevatorsNoPassenger[i][0]<<" Distance: " << stationaryElevatorsNoPassenger[i][8]<<endl;
        }

        //If there is an idle elevator, assign this to people requesting it first.
        if(stationaryElevatorsNoPassenger.size() > 0){
            for(size_t i = 0; i < stationaryElevatorsNoPassenger.size(); i++){
                closestElevator = stationaryElevatorsNoPassenger[i][0];
                break;
            }
        }
        //Otherwise, go through the logic to decide which elevator to assign
        else {

            for (size_t i = 0; i < candidateElevatorsLength; i++) {
                //request elevator status
                int totalCapacity = stoi(elevators[i][4]);
                int remainingCapacity = stoi(candidateElevators[i][5]);
                int currentFloor = stoi(candidateElevators[i][3]);
                int passengerCount = stoi(elevators[i][9]);
                string directionString = candidateElevators[i][6];

                //Check if the remaining capacity is greater than zero
                if (remainingCapacity > 0) {

                    // Check if the elevator is moving in the same direction as the person
                    bool sameDirection = (
                            (directionString == needUpOrDown && currentFloor < endFloor && needUpOrDown == "U") ||
                            (directionString == needUpOrDown && currentFloor > endFloor && needUpOrDown == "D"));

                    //If the elevator is moving in the same direction as person, then assign the elevator to that person.
                    if (sameDirection) {
                        cout << "\n\nIf logic\n\n" << endl;
                        closestElevator = candidateElevators[i][0];
                        //Once chosen, break the loop.
                        break;
                    } else{//Otherwise sort the candidate elevators by remaining capacity in descending order
                        cout << "\n\nElse If logic\n\n" << endl;
                        sort(candidateElevators.begin(), candidateElevators.end(), sortByRemainingCapacity);
                        cout<<"Candidate elevators sorted by remainingCapacity: "<<endl;
                        for(int i = 0; i < candidateElevators.size(); i++){
                            cout<<"ID :"<<candidateElevators[i][0]<<"\nremainingCapcity: " << candidateElevators[i][5]<<endl;
                        }
                        //Choose the elevator with the highest remaining capacity
                        closestElevator = candidateElevators[0][0];
                        //Once chosen, break the loop.
                        break;
                    }
                }
            }
        }

        //Push the person with elevator assigned into the global queue.
        string nextPerson = personID + "/" + closestElevator;
        assignedElevator.push_back(nextPerson);
        cv_addToElevator.notify_all();

        cout<<"\nnext person with elevator assigned: "<<nextPerson<<+"/"+closestElevator<<endl;

        //Once the person has been assigned an elevator, removed them from the queue.
        people.pop_front();
    }

    lock_guard<std::mutex> lock(mtx);
    // use a variable to indicate if the reader reached the end of file
    everyoneAssignedElevator = true;
    // then notify the worker threads
    cv_addToElevator.notify_all();
}


//This function sends a request to the simulator to put them into an elevator
void add_person_to_elevator(){
    while(true){
        // lock the shared resources to make sure only one thread at a time accesses them
        unique_lock<mutex> lock(mtx);
        // wait if the shared buffer is empty and the reader has not reached the end of file yet
        while(assignedElevator.empty() && everyoneAssignedElevator == false) {
            cv_addToElevator.wait(lock);
        }
        //If everyone was assigned an elevator and assignedElevator queue is empty, we can break the loop.
        if(everyoneAssignedElevator == true && assignedElevator.empty()){
            break;
        }
        cout<<"\n\nserving "<<assignedElevator.front()<<endl;

        //Make the curl request to put the person in an assigned elavator.
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

    //Push the elevators into a queue.
    string line;
    while (getline(file, line)) {
        istringstream iss(line);
        string token;

        // queue for elevator
        deque<string> elevator;
        while (std::getline(iss, token, '\t')) {
            elevator.push_back(token);
        }
        // Push the current elevator vector to elevators
        elevators.push_back(elevator);
    }

    //initialize some attributes that we will need to make elevator assignment decision later.
    for(int i = 0; i < elevators.size(); i++){
        elevators[i].push_back("");
        elevators[i].push_back("");
        elevators[i].push_back("");
        elevators[i].push_back("");
        elevators[i].push_back("");
    }

    // Close the file
    file.close();

    for(int i = 0; i < elevators.size(); i++){
        for(int j = 0; j < elevators[0].size(); j++){
            cout << elevators[i][j]<<"\t";
        }
        cout<<endl;
    }

    //Start the simulation.
    init_put("http://localhost:5432/Simulation/start");

    //Use multiple threads to put the people into an elevator.
    thread read(reader);
    thread schedule(schedule_elevator);
    thread addToElevator(add_person_to_elevator);

    //Join the threads.
    read.join();
    schedule.join();
    addToElevator.join();

    return 0;
}
