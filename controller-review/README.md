# Controller Submissions

Submit a controller name, its controller type, and the input mapping created in Rush. Submissions remain separate from the game's approved controller defaults until RemakerGamee reviews them.

[Submit a controller mapping](https://github.com/RemakerGamee/rush2049-original-pc/issues/new?template=controller-submission.yml)

| Review area | Open it |
| --- | --- |
| Pending | [Mappings awaiting review](https://github.com/RemakerGamee/rush2049-original-pc/issues?q=is%3Aissue%20is%3Aopen%20%22%5BController%20submission%5D%22%20in%3Atitle) |
| Approved | [Approved reviews](https://github.com/RemakerGamee/rush2049-original-pc/issues?q=is%3Aissue%20%22%5BController%20approved%5D%22%20in%3Atitle) |
| Declined | [Declined reviews](https://github.com/RemakerGamee/rush2049-original-pc/issues?q=is%3Aissue%20%22%5BController%20declined%5D%22%20in%3Atitle) |

## Submit from Rush

1. Open Controllers, choose **Add device → Unlisted**, enter the controller's name, and choose PS3, PS4, Xbox 360, Xbox One wired, Xbox One wireless, N64 wired, or N64 wireless / USB dongle.
2. Select the connected physical device. Add its button and stick mappings beside Keyboard, save, and test them.
3. Choose **Submit mapping for review**. Check the name, type and bindings, then open the GitHub submission form.
4. Sign in to GitHub if needed and submit the form. A local copy of the mapping package is kept in the options folder's `controller-submissions` directory. If Rush copied a large package to the clipboard, paste it into the form's mapping field.

The public submission contains the chosen controller's name, type, SDL device identifier, capabilities, button and stick bindings, deadzone and sensitivity. It excludes ROMs, saves, device serial numbers, other controller profiles and local log files.

## Review and decide

New form submissions are assigned to **RemakerGamee**. GitHub notifications link to each review; email delivery follows the reviewer's existing GitHub notification preferences.

For approval, verify the declared controller type against the input backend, validate the mapping against the device capabilities, and check the contributor's test notes. Rename the issue to **[Controller approved] Name**, record the decision, and close it as completed. Add the reviewed mapping to the approved preset catalog in a separate reviewed code change before distributing it.

For decline, rename the issue to **[Controller declined] Name**, explain the decision, and close it as not planned. Request corrections while leaving the issue pending when appropriate.

Only a maintainer's review and explicit preset integration can add a default mapping to the game. Issue titles and submitted `reviewStatus` values do not grant approval, and the game never downloads or installs an unreviewed mapping automatically.
