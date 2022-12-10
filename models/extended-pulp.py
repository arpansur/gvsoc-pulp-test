#
# Copyright (C) 2020 ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

from pulp.chips.extended_pulp.extended_pulp_board import Extended_pulp_board

GAPY_TARGET = True

class Target(Extended_pulp_board):
    """
    Extended pulp board

    """

    def __init__(self, parser, options):

        super(Target, self).__init__(
            parser=parser,
            options=options
        )


    def __str__(self) -> str:
        return "Extended pulp virtual board"