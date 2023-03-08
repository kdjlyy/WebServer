#include <bits/stdc++.h>
using namespace std;

class A {
public:
    A() { cout << "A()" << endl; }
    A(int _a) {
        a = _a;
        cout << "A(_a)" << endl;
    }

private:
    int a;
};

class B {
public:
    B() { cout << "B()" << endl; }
    B(int _b) : b(_b), m_a(_b) {
        // b = _b, m_a = A(_b);
        cout << "B(_b)" << endl;
    }

private:
    int b;
    A m_a;
};

int main(int argc, char** argv) {
    B b(100);
}