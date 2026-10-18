-- uVim.app: opens uVim in its own terminal window (Alacritty when
-- installed, otherwise Terminal). Files dropped on the app or opened with
-- it are passed to uVim. The work is done by uvim-launch.sh in Resources.

on run
	launchUvim({})
end run

on open droppedItems
	set filePaths to {}
	repeat with droppedItem in droppedItems
		set end of filePaths to POSIX path of droppedItem
	end repeat
	launchUvim(filePaths)
end open

on launchUvim(filePaths)
	set launcher to POSIX path of (path to resource "uvim-launch.sh")
	set command to quoted form of launcher
	repeat with filePath in filePaths
		set command to command & " " & quoted form of (filePath as text)
	end repeat
	do shell script command
end launchUvim
