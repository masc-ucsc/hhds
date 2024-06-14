#include <iostream>

class MyClass {
private:
    void privateFunction() {
        std::cout << "Private function called" << std::endl;
        // Calling a public member function from a private member function
        publicFunction();
    }
public:
    void publicFunction() {
        std::cout << "Public function called" << std::endl;
    }

    void callPrivateFunction() {
        privateFunction();
    }

};

int main() {
    MyClass obj;
    obj.callPrivateFunction();  // This will call the private function indirectly
    return 0;
}
