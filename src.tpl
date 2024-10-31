<%
/* stuff happens here ... */
var username = "Nobody";
%>
<!DOCTYPE html>
<html>
    <head>
        <title>Blah</title>
    </head>
    <body>
        <h3>Goodbye <%= username %>!</h3>
        <table>
            <thead>
                <tr>
                    <td>
                        <b>
                            Key
                        </b>
                    </td>
                </tr>
            </thead>
            <tbody>
            <% foreach(key, val in Process.env) {  %>
                <tr>
                    <td>
                        <%= key %>
                    </td>
                    <td>
                        <em>
                            <%= val %>
                        </em>
                    </td>
                </tr>
            <%}%>
            </tbody>
        </ul>
    </body>
</html>