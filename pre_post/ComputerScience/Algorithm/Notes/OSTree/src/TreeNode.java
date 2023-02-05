
class TreeNode<T> {
    public long key;
    private T value;
    public TreeNode<T> right,left,parent;
    public Color color;
    public int size;//记录子树（包括本身）的大小
    public TreeNode(long k,T v){
        // TODO Auto-generated constructor stub
        key = k;
        value = v;
        color=Color.BLACK;
        size=0;
    }
    public T getValue() {
        return value;
    }
    public void setValue(T value) {
        this.value = value;
    }
    public boolean equals(Object o) {
        if (this==o) {
            return true;
        }
        if (o==null) {
            return false;
        }
        if (!(o instanceof TreeNode<?>)) {
            return false;
        }
        TreeNode<T> ent;
        try {
            ent = (TreeNode<T>)  o; // 检测类型
        } catch (ClassCastException ex) {
            return false;
        }
        return (ent.key == key) && (ent.getValue() == value);
    }
    @Override
    public String toString() {
        // TODO Auto-generated method stub
        return key+":"+value+":"+color+":"+size;
    }
}