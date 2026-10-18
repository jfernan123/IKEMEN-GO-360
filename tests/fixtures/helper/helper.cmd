[Command]
name = "spawn"
command = x
time = 1

[Statedef -1]

[State -1, spawn]
type = ChangeState
value = 1000
triggerall = command = "spawn"
trigger1 = stateno = 0

[Statedef 1000]
type = S
movetype = I
physics = S
anim = 0
ctrl = 0

[State 1000, helper]
type = Helper
trigger1 = time = 0
persistent = 0
name = "Child"
id = 77
stateno = 1100
postype = p1
pos = 40, 0

[State 1000, redirects complete]
type = ChangeState
value = 1002
trigger1 = var(5) = 123
trigger1 = var(6) = 456
trigger1 = numhelper = 2
trigger1 = helper(77), var(7) = 789

[Statedef 1002]
type = S
movetype = I
physics = S
anim = 0
ctrl = 0

[State 1002, done]
type = ChangeState
value = 0
trigger1 = time = 20
