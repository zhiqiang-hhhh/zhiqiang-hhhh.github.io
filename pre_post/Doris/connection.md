```java
public void handleEvent(AcceptingChannel<StreamConnection> channel) {
    ...
    ConnectContext context = new ConnectContext(connection);
    context.setEnv(Env.getCurrentEnv());
    connectScheduler.submit(context);
    
}
```