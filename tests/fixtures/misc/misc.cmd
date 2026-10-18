[Command]
name = "go"
command = x
time = 1

[Statedef -1]

[State -1, go]
type = ChangeState
value = 1000
triggerall = command = "go"
trigger1 = stateno = 0
