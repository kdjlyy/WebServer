// 升序双向链表
#include <iostream>

using namespace std;

// 双向链表结点结构体
struct ListNode {
    int val;                                                  // 存储的值
    ListNode *prev, *next;                                    // 指向前后结点的指针
    ListNode(int x) : val(x), prev(nullptr), next(nullptr) {} // 构造函数
};

// 双向链表类
class LinkedList {
private:
    ListNode* head; // 头结点
    ListNode* tail; // 尾结点
public:
    // 构造函数
    LinkedList() {
        // 初始化头尾结点
        head = new ListNode(0);
        tail = new ListNode(0);
        head->next = tail;
        tail->prev = head;
    }

    // 插入结点
    void insert(int val) {
        ListNode* curr = head->next;
        // 找到插入位置
        while (curr != tail && curr->val < val) {
            curr = curr->next;
        }
        // 创建新结点
        ListNode* node = new ListNode(val);
        // 插入结点
        node->next = curr;
        node->prev = curr->prev;
        curr->prev->next = node;
        curr->prev = node;
    }

    // 删除结点
    void remove(int val) {
        ListNode* curr = head->next;
        // 查找待删除结点
        while (curr != tail && curr->val != val) {
            curr = curr->next;
        }
        // 删除结点
        if (curr != tail && curr->val == val) {
            curr->prev->next = curr->next;
            curr->next->prev = curr->prev;
            delete curr;
        }
    }

    // 删除最小节点
    void remove_minimum() {
        ListNode* curr = head->next;
        head->next = curr->next;
        curr->next->prev = head;
        delete curr;
    }

    // 输出链表
    void
        print() {
        ListNode* curr = head->next;
        while (curr != tail) {
            cout << curr->val << " ";
            curr = curr->next;
        }
        cout << endl;
    }
};

int main() {
    LinkedList list;
    list.insert(1);
    list.insert(4);
    list.insert(2);
    list.insert(3);
    list.remove(4);
    list.remove_minimum();
    list.print();
    return 0;
}
