public class Main {

    public static void main(String[] args) {
        // TODO Auto-generated method stub
        OSTree<Message> tree=new OSTree<Message>();
        byte b[] = {123};

        for(int i=0; i<=55; i++){

            Message msg = new Message(i*100, i*10, b );
            tree.insert(msg.getT(), msg);
        }


        System.out.println(tree);
        System.out.println("输出第1、10、20号元素");
        System.out.println(tree.select(1));
        System.out.println(tree.select(10));
        System.out.println(tree.select(20));

    }
}

