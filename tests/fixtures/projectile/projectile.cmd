[Defaults]
command.time = 15
command.buffer.time = 1

[Command]
name = "fire"
command = x
time = 1

[Command]
name = "clash"
command = y
time = 1

[Statedef -1]

[State -1, fire]
type = ChangeState
value = 1000
triggerall = ctrl
trigger1 = command = "fire"

[State -1, clash]
type = ChangeState
value = 1001
triggerall = ctrl
trigger1 = command = "clash"
