extends Node2D

func _ready() -> void:
	$camera3d.initialize()
	$Tween.interpolate_property(
		$icon,
		"rotation",
		$icon.rotation,
		$icon.rotation + 2 * PI,
		1, Tween.TRANS_LINEAR, Tween.EASE_IN)
	$Tween.start()
	$camera3d.start_recorder()

func _process(delta: float) -> void:
	if $camera3d.is_started():
		$camera3d.recorder_step()
	if $camera3d.get_received_frame_count() >= 60 * 3:
		$camera3d.stop_recorder()
		set_process(false)

