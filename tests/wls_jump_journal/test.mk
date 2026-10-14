# Copyright 2026
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# --------------------------------------------------------------------------------
# Keep this file, even if it is empty, as a marker that this folder contains tests
# --------------------------------------------------------------------------------

# The journal encoder is hardware-free by construction (no include beyond
# stdint.h), so it is added directly. The `.ram0` storage and stamping live in
# the shell (`wls_jump_journal.c`) and are not built here.
SRC += \
	$(TOP_DIR)/keyboards/epomaker/epomaker_split65/wls/jump_journal_logic.c
