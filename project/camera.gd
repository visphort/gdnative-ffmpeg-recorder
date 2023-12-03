extends Camera2D

# Declare member variables here. Examples:
# var a: int = 2
# var b: String = "text"

var do_once: bool = true


# Called when the node enters the scene tree for the first time.
func _process(delta: float) -> void:
	if Engine.get_frames_drawn() == 0:
		return
	if do_once:
		var image = get_viewport().get_texture().get_data()
		print(image.data["format"])
		do_once = false
