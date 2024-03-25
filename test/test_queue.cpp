#include <cstdlib>
#include <iostream>
#include "test_utils.hpp"
#include "../src/queue.hpp"

int main()
{
    {
        Queue<int, 0> q;
        test(q.push(10) == false);
        test(q.size() == 0);
        test(q.empty() == true);
        test(q.pop() == false);
    }

    {
        Queue<int, 1> q;
        test(q.size() == 0);
        test(q.empty() == true);
        test(q.push(10) == true);
        test(q.size() == 1);
        test(q.empty() == false);
        test(q.push(4) == false);
        test(q.size() == 1);
        test(q.empty() == false);
        test(q.pop() == true);
        test(q.pop() == false);
    }

    {
        Queue<int, 4> q;

        test(q.push(1) == true); 
        test(q.size() == 1);
        
        test(q.push(2) == true); 
        test(q.size() == 2);

        test(q.push(3) == true); 
        test(q.size() == 3);
        
        test(q.push(4) == true);
        test(q.size() == 4);

        int x;

        test(q.pop(x) == true);
        test(x == 1);
        test(q.size() == 3);

        test(q.push(5) == true);
        test(q.size() == 4);

        test(q.pop(x) == true);
        test(x == 2);
        test(q.size() == 3);

        test(q.push(6) == true);
        test(q.size() == 4);
        
        test(q.pop(x) == true);
        test(x == 3);
        test(q.size() == 3);

        test(q.push(7) == true);
        test(q.size() == 4);

        test(q.pop(x) == true);
        test(x == 4);
        test(q.size() == 3);

        test(q.push(8) == true);
        test(q.size() == 4);

        test(q.pop(x) == true);
        test(x == 5);
        test(q.size() == 3);

        test(q.push(9) == true);
        test(q.size() == 4);

        test(q.pop(x) == true);
        test(x == 6);
        test(q.size() == 3);

        test(q.push(10) == true);
        test(q.size() == 4);

        test(q.pop(x) == true);
        test(x == 7);
        test(q.size() == 3);

        test(q.pop(x) == true);
        test(x == 8);
        test(q.size() == 2);

        test(q.pop(x) == true);
        test(x == 9);
        test(q.size() == 1);

        test(q.pop(x) == true);
        test(x == 10);
        test(q.size() == 0);

        test(q.pop(x) == false);
    }

    std::cout << "Passed\n";
}